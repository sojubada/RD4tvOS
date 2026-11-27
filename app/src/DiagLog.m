#import "DiagLog.h"

#import <os/log.h>
#import <sys/socket.h>
#import <netinet/in.h>
#import <arpa/inet.h>
#import <unistd.h>
#import <mach/mach.h>
#import <errno.h>

#define DL_RING_MAX 400

static NSString *const kDefaultsLevels = @"rd4tvos.diag.levels";
static NSString *const kDefaultsSinkHost = @"rd4tvos.diag.sinkHost";
static NSString *const kDefaultsSinkPort = @"rd4tvos.diag.sinkPort";
static NSString *const kDefaultsLastError = @"rd4tvos.diag.lastError";

@implementation DiagLog

static os_log_t      s_logs[DLCatCount];
static DLLevel       s_levels[DLCatCount];
static NSString     *s_traceId = @"-";
static NSMutableArray<NSString *> *s_ring;
static NSString     *s_logPath;
static NSFileHandle *s_logFile;
static NSString     *s_sinkHost;
static uint16_t      s_sinkPort;
static int           s_sinkSock = -1;
static dispatch_queue_t s_q;
static NSUInteger    s_written;
static NSUInteger    s_dropped;
static NSUInteger    s_sinkSent;
static NSUInteger    s_sinkFailed;

static const char *kCatNames[DLCatCount] = {
    "Lifecycle", "Network", "Auth", "Input", "Render", "Storage", "UI"
};

+ (NSString *)categoryName:(DLCategory)cat
{
    if (cat < 0 || cat >= DLCatCount) return @"?";
    return [NSString stringWithUTF8String:kCatNames[cat]];
}

+ (NSString *)levelName:(DLLevel)level
{
    switch (level) {
        case DLLevelError: return @"ERROR";
        case DLLevelWarn:  return @"WARN";
        case DLLevelInfo:  return @"INFO";
        default:           return @"DEBUG";
    }
}

+ (void)start
{
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        s_q = dispatch_queue_create("rd4tvos.diag", DISPATCH_QUEUE_SERIAL);
        s_ring = [NSMutableArray arrayWithCapacity:DL_RING_MAX];
        NSString *subsystem = [[NSBundle mainBundle] bundleIdentifier] ?: @"rd4tvos";
        for (NSInteger i = 0; i < DLCatCount; i++) {
            s_logs[i] = os_log_create(subsystem.UTF8String, kCatNames[i]);
            s_levels[i] = DLLevelInfo;   // 기본값. debug 는 꺼 둔다.
        }
        [self restoreSettings];
        [self openLogFile];
        [self openSink];
    });

    // 지난 세션이 비정상 종료했다면 그 마지막 줄이 여기 남아 있다.
    NSString *last = [[NSUserDefaults standardUserDefaults] stringForKey:kDefaultsLastError];
    if (last.length) {
        DLogW(DLCatLifecycle, @"이전 세션 마지막 오류: %@", last);
    }

    DLogI(DLCatLifecycle, @"diag start logPath=%@ sink=%@ footprint=%@",
          s_logPath ?: @"(없음)", [self sinkDescription] ?: @"(없음)", [self footprintString]);
}

#pragma mark - 설정 보존 (Library/Caches 는 비워질 수 있으므로 환경설정에 둔다)

+ (void)restoreSettings
{
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    NSArray *levels = [d arrayForKey:kDefaultsLevels];
    if (levels.count == DLCatCount) {
        for (NSInteger i = 0; i < DLCatCount; i++) {
            s_levels[i] = (DLLevel)[levels[i] integerValue];
        }
    }
    s_sinkHost = [d stringForKey:kDefaultsSinkHost];
    s_sinkPort = (uint16_t)[d integerForKey:kDefaultsSinkPort];
}

+ (void)persistSettings
{
    NSUserDefaults *d = [NSUserDefaults standardUserDefaults];
    NSMutableArray *levels = [NSMutableArray arrayWithCapacity:DLCatCount];
    for (NSInteger i = 0; i < DLCatCount; i++) {
        [levels addObject:@(s_levels[i])];
    }
    [d setObject:levels forKey:kDefaultsLevels];
    if (s_sinkHost) [d setObject:s_sinkHost forKey:kDefaultsSinkHost];
    [d setInteger:s_sinkPort forKey:kDefaultsSinkPort];
}

#pragma mark - 경로 B: 앱 컨테이너 안의 진단 폴더

+ (void)openLogFile
{
    NSArray *dirs = NSSearchPathForDirectoriesInDomains(NSCachesDirectory, NSUserDomainMask, YES);
    if (!dirs.count) {
        // 조용히 넘어가면 로그가 없는 이유를 모른다. 실패 자체를 남긴다.
        [self error:DLCatStorage domain:@"diag" code:-1 format:@"Caches 경로를 찾지 못했다"];
        return;
    }
    NSString *dir = [dirs.firstObject stringByAppendingPathComponent:@"Diag"];
    NSError *err = nil;
    if (![[NSFileManager defaultManager] createDirectoryAtPath:dir
                                  withIntermediateDirectories:YES
                                                   attributes:nil
                                                        error:&err]) {
        [self error:DLCatStorage domain:err.domain ?: @"diag" code:err.code
             format:@"진단 폴더 생성 실패 path=%@", dir];
        return;
    }

    NSDateFormatter *df = [NSDateFormatter new];
    df.dateFormat = @"yyyyMMdd";
    NSString *name = [NSString stringWithFormat:@"%@.log", [df stringFromDate:[NSDate date]]];
    s_logPath = [dir stringByAppendingPathComponent:name];

    if (![[NSFileManager defaultManager] fileExistsAtPath:s_logPath]) {
        [[NSFileManager defaultManager] createFileAtPath:s_logPath contents:nil attributes:nil];
    }
    s_logFile = [NSFileHandle fileHandleForWritingAtPath:s_logPath];
    [s_logFile seekToEndOfFile];
}

+ (nullable NSString *)logFilePath { return s_logPath; }

#pragma mark - 경로 C: NAS 로 직접 전송

+ (void)openSink
{
    if (s_sinkSock >= 0) { close(s_sinkSock); s_sinkSock = -1; }
    if (!s_sinkHost.length || s_sinkPort == 0) return;
    s_sinkSock = socket(AF_INET, SOCK_DGRAM, 0);
    if (s_sinkSock < 0) {
        [self error:DLCatStorage domain:@"posix" code:errno format:@"UDP 소켓 생성 실패"];
    }
}

+ (void)setSinkHost:(nullable NSString *)host port:(uint16_t)port
{
    NSString *before = [self sinkDescription] ?: @"(없음)";
    s_sinkHost = host.length ? host : nil;
    s_sinkPort = port;
    [self openSink];
    [self persistSettings];
    DLogI(DLCatStorage, @"log sink %@ -> %@", before, [self sinkDescription] ?: @"(없음)");
}

+ (nullable NSString *)sinkDescription
{
    if (!s_sinkHost.length || s_sinkPort == 0) return nil;
    return [NSString stringWithFormat:@"udp://%@:%u", s_sinkHost, (unsigned)s_sinkPort];
}

// 전송 실패는 무시한다. 앱이 로그 때문에 죽으면 안 된다.
+ (void)sendToSink:(NSString *)line
{
    if (s_sinkSock < 0 || !s_sinkHost.length) return;
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(s_sinkPort);
    if (inet_pton(AF_INET, s_sinkHost.UTF8String, &addr.sin_addr) != 1) {
        s_sinkFailed++;
        return;
    }
    NSData *d = [line dataUsingEncoding:NSUTF8StringEncoding];
    ssize_t n = sendto(s_sinkSock, d.bytes, d.length, 0, (struct sockaddr *)&addr, sizeof(addr));
    if (n < 0) s_sinkFailed++; else s_sinkSent++;
}

#pragma mark - 레벨

+ (void)setLevel:(DLLevel)level forCategory:(DLCategory)cat
{
    if (cat < 0 || cat >= DLCatCount) return;
    DLLevel before = s_levels[cat];
    s_levels[cat] = level;
    [self persistSettings];
    DLogI(DLCatLifecycle, @"log.level %@ %@ -> %@", [self categoryName:cat],
          [self levelName:before], [self levelName:level]);
}

+ (void)setLevelForAll:(DLLevel)level
{
    for (NSInteger i = 0; i < DLCatCount; i++) s_levels[i] = level;
    [self persistSettings];
    DLogI(DLCatLifecycle, @"log.level ALL -> %@", [self levelName:level]);
}

+ (DLLevel)levelForCategory:(DLCategory)cat
{
    return (cat >= 0 && cat < DLCatCount) ? s_levels[cat] : DLLevelError;
}

#pragma mark - traceId

+ (NSString *)newTraceId
{
    uint32_t r = arc4random();
    s_traceId = [NSString stringWithFormat:@"%06x", (unsigned)(r & 0xFFFFFF)];
    return s_traceId;
}

+ (NSString *)traceId { return s_traceId; }

#pragma mark - 기록

+ (NSString *)footprintString
{
    task_vm_info_data_t info;
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return [NSString stringWithFormat:@"%lluMB", (unsigned long long)(info.phys_footprint >> 20)];
    }
    return @"?";
}

+ (void)emit:(DLCategory)cat level:(DLLevel)level body:(NSString *)body
{
    NSString *line = [NSString stringWithFormat:@"%.3f | %-5@ | %-9@ | %@ | %@",
                      [NSDate date].timeIntervalSince1970,
                      [self levelName:level], [self categoryName:cat], s_traceId, body];

    os_log_t log = s_logs[cat];
    switch (level) {
        case DLLevelError: os_log_error(log, "%{public}s", line.UTF8String); break;
        case DLLevelWarn:  os_log(log,       "%{public}s", line.UTF8String); break;
        case DLLevelInfo:  os_log_info(log,  "%{public}s", line.UTF8String); break;
        default:           os_log_debug(log, "%{public}s", line.UTF8String); break;
    }

    dispatch_async(s_q, ^{
        s_written++;
        @synchronized (s_ring) {
            [s_ring addObject:line];
            while (s_ring.count > DL_RING_MAX) [s_ring removeObjectAtIndex:0];
        }
        if (s_logFile) {
            @try {
                [s_logFile writeData:[[line stringByAppendingString:@"\n"]
                                         dataUsingEncoding:NSUTF8StringEncoding]];
            } @catch (NSException *e) {
                // 파일 기록 실패가 앱을 죽이지 않게 한다
            }
        }
        [self sendToSink:line];
    });
}

+ (void)log:(DLCategory)cat level:(DLLevel)level format:(NSString *)fmt, ...
{
    if (cat < 0 || cat >= DLCatCount) return;
    if (level > s_levels[cat]) { s_dropped++; return; }
    va_list ap;
    va_start(ap, fmt);
    NSString *body = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    [self emit:cat level:level body:body];
}

+ (void)error:(DLCategory)cat domain:(NSString *)domain code:(NSInteger)code
       format:(NSString *)fmt, ...
{
    va_list ap;
    va_start(ap, fmt);
    NSString *msg = [[NSString alloc] initWithFormat:fmt arguments:ap];
    va_end(ap);
    NSString *body = [NSString stringWithFormat:@"domain=%@ code=%ld msg=%@",
                      domain ?: @"?", (long)code, msg];
    [self emit:cat level:DLLevelError body:body];

    // 강제종료·행업의 마지막 지점을 남긴다. Caches 는 비워질 수 있으므로 환경설정에.
    [[NSUserDefaults standardUserDefaults] setObject:body forKey:kDefaultsLastError];
}

+ (void)logSessionSummary:(NSDictionary<NSString *, id> *)counters
{
    NSMutableString *s = [NSMutableString stringWithFormat:@"session=%@", s_traceId];
    for (NSString *k in [counters.allKeys sortedArrayUsingSelector:@selector(compare:)]) {
        [s appendFormat:@" %@=%@", k, counters[k]];
    }
    [s appendFormat:@" log_written=%lu log_dropped=%lu sink_sent=%lu sink_failed=%lu mem=%@",
                    (unsigned long)s_written, (unsigned long)s_dropped,
                    (unsigned long)s_sinkSent, (unsigned long)s_sinkFailed,
                    [self footprintString]];
    [self emit:DLCatLifecycle level:DLLevelInfo body:s];
}

+ (NSArray<NSString *> *)recentLines:(NSUInteger)count
{
    @synchronized (s_ring) {
        NSUInteger n = MIN(count, s_ring.count);
        return [s_ring subarrayWithRange:NSMakeRange(s_ring.count - n, n)];
    }
}

+ (NSString *)statusSummary
{
    NSMutableString *s = [NSMutableString string];
    for (NSInteger i = 0; i < DLCatCount; i++) {
        [s appendFormat:@"%@=%@ ", [self categoryName:(DLCategory)i],
                        [self levelName:s_levels[i]]];
    }
    [s appendFormat:@"\n파일: %@\n전송: %@ (성공 %lu / 실패 %lu)\n메모리: %@",
                    s_logPath ?: @"(없음)", [self sinkDescription] ?: @"(없음)",
                    (unsigned long)s_sinkSent, (unsigned long)s_sinkFailed,
                    [self footprintString]];
    return s;
}

@end
