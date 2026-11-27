// DiagLog.h — 진단 로그 인프라
//
// 플레이북 §7: 로그는 기능이 아니라 인프라다. 애플TV 는 릴리즈 URL 로만 설치되므로
// 로그 한 줄이 릴리즈 한 건이 된다. 목표는 "원인을 보려고 릴리즈한다"를 없애는 것이고,
// 그러려면 세 축을 같이 갖춰야 한다. 하나만 하면 릴리즈가 다시 늘어난다.
//
//   ① 범위 — 처음부터 전 기능에 깔고 그 뒤로 손대지 않는다.
//   ② 수집 — 설치 없이 기기 로그를 읽는다 (NAS 전송 / 앱 컨테이너 파일 / 화면).
//   ③ 스위치 — 카테고리·레벨·전송지를 런타임에 바꾼다.
//
// #if DEBUG 를 쓰지 않는다. 여기서는 릴리즈 빌드가 곧 실사용 바이너리다.
// DEBUG 로 감싸면 정작 버그가 나는 바이너리에서 로그가 사라진다.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

typedef NS_ENUM(NSInteger, DLCategory) {
    DLCatLifecycle = 0,   // 기동·종료·복원된 설정·footprint
    DLCatNetwork,         // 접속 단계, 바이트·소요 ms, 재연결
    DLCatAuth,            // 인증 협상, NLA 단계. 비밀번호는 마스킹
    DLCatInput,           // 마우스·키보드·조이스틱 수신과 변환
    DLCatRender,          // 프레임·드로잉 ms·드롭
    DLCatStorage,         // 진단 폴더 생성 성공 여부, 설정 보존
    DLCatUI,              // 화면 진입, 포커스 엔진 개입
    DLCatCount
};

typedef NS_ENUM(NSInteger, DLLevel) {
    DLLevelError = 0,
    DLLevelWarn  = 1,
    DLLevelInfo  = 2,
    DLLevelDebug = 3
};

@interface DiagLog : NSObject

+ (void)start;

// 한 사용자 동작마다 발급한다. 시작~실패 지점이 한 줄로 엮여 보이게 하는 장치다.
+ (NSString *)newTraceId;
+ (NSString *)traceId;

+ (void)setLevel:(DLLevel)level forCategory:(DLCategory)cat;
+ (void)setLevelForAll:(DLLevel)level;
+ (DLLevel)levelForCategory:(DLCategory)cat;
+ (NSString *)categoryName:(DLCategory)cat;
+ (NSString *)levelName:(DLLevel)level;

+ (void)log:(DLCategory)cat level:(DLLevel)level format:(NSString *)fmt, ... NS_FORMAT_FUNCTION(3, 4);

// 오류는 레벨에 상관없이 항상 남기고 domain·code·설명 3종을 고정으로 붙인다.
+ (void)error:(DLCategory)cat domain:(NSString *)domain code:(NSInteger)code
       format:(NSString *)fmt, ... NS_FORMAT_FUNCTION(4, 5);

// 수집 경로 C — 기기에 아무것도 쓰지 않으므로 저장 경로 문제를 아예 피한다.
+ (void)setSinkHost:(nullable NSString *)host port:(uint16_t)port;
+ (nullable NSString *)sinkDescription;

// 수집 경로 B — 앱 자신의 컨테이너 안. /var/mobile/Media 아래는 쓸 수 없다.
+ (nullable NSString *)logFilePath;

+ (NSArray<NSString *> *)recentLines:(NSUInteger)count;
+ (NSString *)statusSummary;

// 세션 요약 한 줄. 원인을 가를 숫자를 넣는다 — 숫자 없는 로그는 다음 릴리즈를 부른다.
+ (void)logSessionSummary:(NSDictionary<NSString *, id> *)counters;

@end

// 매크로 파라미터 이름이 Objective-C 셀렉터 키워드와 같으면 전처리기가 셀렉터까지
// 치환한다. code:(code) 가 -1:(-1) 이 되어 'error:domain::format:' 을 찾는 오류가 났다.
// 그래서 파라미터 이름 뒤에 밑줄을 붙인다.
#define DLogI(cat_, ...) [DiagLog log:(cat_) level:DLLevelInfo  format:__VA_ARGS__]
#define DLogW(cat_, ...) [DiagLog log:(cat_) level:DLLevelWarn  format:__VA_ARGS__]
#define DLogD(cat_, ...) [DiagLog log:(cat_) level:DLLevelDebug format:__VA_ARGS__]
#define DLogE(cat_, dom_, code_, ...) \
    [DiagLog error:(cat_) domain:(dom_) code:(code_) format:__VA_ARGS__]

NS_ASSUME_NONNULL_END
