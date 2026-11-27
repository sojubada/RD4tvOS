#import "DiagViewController.h"

#import "DiagLog.h"
#import "KeyboardProbe.h"
#import "NtlmSelfTest.h"

// 55인치 4K 를 약 3m 거리에서 본다. 글자는 굵고 크게.
static const CGFloat kTitleSize = 52;
static const CGFloat kRowNameSize = 30;
static const CGFloat kRowDetailSize = 22;
static const CGFloat kStatusSize = 26;
static const CGFloat kLogSize = 18;

@interface DiagViewController ()
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UILabel *verdictLabel;
@property (nonatomic, strong) UIStackView *buttonRow;
@property (nonatomic, strong) UIStackView *resultStack;
@property (nonatomic, strong) UILabel *keyboardLabel;
@property (nonatomic, strong) UILabel *logLabel;
@property (nonatomic, assign) BOOL running;
@end

@implementation DiagViewController

- (void)viewDidLoad
{
    [super viewDidLoad];
    self.view.backgroundColor = [UIColor colorWithWhite:0.07 alpha:1.0];
    DLogI(DLCatUI, @"diag 화면 진입");

    [self buildTitle];
    [self buildButtons];
    [self buildResults];
    [self buildFooter];

    __weak DiagViewController *weakSelf = self;
    [KeyboardProbe shared].onChange = ^{ [weakSelf refreshFooter]; };
    [[KeyboardProbe shared] start];

    [self refreshFooter];
    [NSTimer scheduledTimerWithTimeInterval:2.0 repeats:YES block:^(NSTimer *t) {
        [weakSelf refreshFooter];
    }];
}

- (UILabel *)labelWithSize:(CGFloat)size weight:(UIFontWeight)w color:(UIColor *)c
{
    UILabel *l = [UILabel new];
    l.font = [UIFont systemFontOfSize:size weight:w];
    l.textColor = c;
    l.numberOfLines = 0;
    return l;
}

- (void)buildTitle
{
    self.titleLabel = [self labelWithSize:kTitleSize weight:UIFontWeightBold
                                    color:[UIColor whiteColor]];
    self.titleLabel.text = @"1a — NTLM 토큰 생성 시험";
    self.titleLabel.frame = CGRectMake(90, 60, 1740, 64);
    [self.view addSubview:self.titleLabel];

    self.verdictLabel = [self labelWithSize:kStatusSize weight:UIFontWeightMedium
                                      color:[UIColor colorWithWhite:0.65 alpha:1.0]];
    self.verdictLabel.text = @"서버도 네트워크도 실제 비밀번호도 필요 없습니다. "
                             @"아래 '시험 실행'을 누르십시오.";
    self.verdictLabel.frame = CGRectMake(90, 128, 1740, 70);
    [self.view addSubview:self.verdictLabel];
}

- (UIButton *)buttonTitled:(NSString *)title action:(SEL)sel
{
    UIButton *b = [UIButton buttonWithType:UIButtonTypeSystem];
    [b setTitle:title forState:UIControlStateNormal];
    b.titleLabel.font = [UIFont systemFontOfSize:28 weight:UIFontWeightSemibold];
    [b addTarget:self action:sel forControlEvents:UIControlEventPrimaryActionTriggered];
    return b;
}

- (void)buildButtons
{
    self.buttonRow = [[UIStackView alloc] initWithFrame:CGRectMake(90, 206, 1740, 70)];
    self.buttonRow.axis = UILayoutConstraintAxisHorizontal;
    self.buttonRow.distribution = UIStackViewDistributionFillEqually;
    self.buttonRow.spacing = 24;
    [self.buttonRow addArrangedSubview:[self buttonTitled:@"시험 실행" action:@selector(runTests)]];
    [self.buttonRow addArrangedSubview:[self buttonTitled:@"로그 레벨" action:@selector(cycleLevel)]];
    [self.buttonRow addArrangedSubview:[self buttonTitled:@"로그 상태" action:@selector(showStatus)]];
    [self.view addSubview:self.buttonRow];
}

- (void)buildResults
{
    self.resultStack = [[UIStackView alloc] initWithFrame:CGRectMake(90, 300, 1740, 520)];
    self.resultStack.axis = UILayoutConstraintAxisVertical;
    self.resultStack.spacing = 14;
    self.resultStack.alignment = UIStackViewAlignmentFill;
    [self.view addSubview:self.resultStack];
}

- (void)buildFooter
{
    self.keyboardLabel = [self labelWithSize:kStatusSize weight:UIFontWeightMedium
                                       color:[UIColor colorWithWhite:0.8 alpha:1.0]];
    self.keyboardLabel.frame = CGRectMake(90, 836, 1740, 40);
    [self.view addSubview:self.keyboardLabel];

    self.logLabel = [self labelWithSize:kLogSize weight:UIFontWeightRegular
                                  color:[UIColor colorWithWhite:0.5 alpha:1.0]];
    self.logLabel.frame = CGRectMake(90, 884, 1740, 140);
    [self.view addSubview:self.logLabel];
}

- (void)refreshFooter
{
    self.keyboardLabel.text = [[KeyboardProbe shared] statusLine];
    NSArray<NSString *> *lines = [DiagLog recentLines:5];
    self.logLabel.text = [lines componentsJoinedByString:@"\n"];
}

#pragma mark - 동작

- (void)runTests
{
    if (self.running) return;
    self.running = YES;

    NSString *trace = [DiagLog newTraceId];
    DLogI(DLCatAuth, @"1a 시험 시작");

    self.verdictLabel.textColor = [UIColor colorWithWhite:0.65 alpha:1.0];
    self.verdictLabel.text = [NSString stringWithFormat:@"시험 중… (trace %@)", trace];
    for (UIView *v in self.resultStack.arrangedSubviews) [v removeFromSuperview];

    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
        NtlmTestResult results[NTLM_SELFTEST_MAX];
        int allPass = 0;
        int n = NtlmSelfTest_Run(results, NTLM_SELFTEST_MAX, &allPass);

        NSMutableArray *rows = [NSMutableArray array];
        NSUInteger pass = 0, fail = 0, skip = 0;
        for (int i = 0; i < n; i++) {
            NSString *name = [NSString stringWithUTF8String:results[i].name] ?: @"?";
            NSString *detail = [NSString stringWithUTF8String:results[i].detail] ?: @"";
            NSString *verdict = [NSString stringWithUTF8String:
                                    NtlmSelfTest_VerdictName(results[i].verdict)];
            switch (results[i].verdict) {
                case NTLM_TEST_PASS: pass++; break;
                case NTLM_TEST_FAIL: fail++; break;
                default: skip++; break;
            }
            // 결과를 로그에도 남긴다 — 화면을 못 보는 상황에서도 NAS 에서 읽는다.
            if (results[i].verdict == NTLM_TEST_FAIL) {
                DLogE(DLCatAuth, @"ntlm-selftest", i, @"%@ | %@", name, detail);
            } else {
                DLogI(DLCatAuth, @"%@ = %@ | %@ | %luus", name, verdict, detail,
                      (unsigned long)results[i].elapsed_us);
            }
            [rows addObject:@{ @"name": name, @"detail": detail,
                               @"verdict": @(results[i].verdict) }];
        }

        [DiagLog logSessionSummary:@{ @"test": @"1a", @"pass": @(pass), @"fail": @(fail),
                                      @"skip": @(skip) }];

        dispatch_async(dispatch_get_main_queue(), ^{
            [self showRows:rows pass:pass fail:fail skip:skip allPass:(allPass != 0)];
            self.running = NO;
        });
    });
}

- (void)showRows:(NSArray<NSDictionary *> *)rows
            pass:(NSUInteger)pass fail:(NSUInteger)fail skip:(NSUInteger)skip
         allPass:(BOOL)allPass
{
    for (NSDictionary *row in rows) {
        NtlmTestVerdict v = (NtlmTestVerdict)[row[@"verdict"] intValue];
        UIColor *c = (v == NTLM_TEST_PASS) ? [UIColor colorWithRed:0.45 green:0.85 blue:0.5 alpha:1]
                   : (v == NTLM_TEST_FAIL) ? [UIColor colorWithRed:1.0 green:0.45 blue:0.4 alpha:1]
                                           : [UIColor colorWithWhite:0.6 alpha:1];
        NSString *mark = (v == NTLM_TEST_PASS) ? @"통과" : (v == NTLM_TEST_FAIL) ? @"실패" : @"건너뜀";

        UILabel *head = [self labelWithSize:kRowNameSize weight:UIFontWeightSemibold color:c];
        head.text = [NSString stringWithFormat:@"%@  —  %@", mark, row[@"name"]];
        head.frame = CGRectMake(0, 0, 1740, 36);
        [self.resultStack addArrangedSubview:head];

        if ([row[@"detail"] length]) {
            UILabel *det = [self labelWithSize:kRowDetailSize weight:UIFontWeightRegular
                                         color:[UIColor colorWithWhite:0.72 alpha:1.0]];
            det.text = row[@"detail"];
            [self.resultStack addArrangedSubview:det];
        }
    }

    if (allPass) {
        self.verdictLabel.textColor = [UIColor colorWithRed:0.45 green:0.85 blue:0.5 alpha:1];
        self.verdictLabel.text = [NSString stringWithFormat:
            @"전부 통과 (%lu) — tvOS 에서 NTLM 토큰이 정상 생성됩니다. 1b 로 넘어갈 수 있습니다.",
            (unsigned long)pass];
    } else {
        self.verdictLabel.textColor = [UIColor colorWithRed:1.0 green:0.45 blue:0.4 alpha:1];
        self.verdictLabel.text = [NSString stringWithFormat:
            @"실패 %lu건 (통과 %lu, 건너뜀 %lu) — 아래 상세를 보고 원인을 가립니다.",
            (unsigned long)fail, (unsigned long)pass, (unsigned long)skip];
    }
    [self refreshFooter];
}

// 재빌드 없이 로그 양을 바꾼다. 여기가 릴리즈 절감의 본체다.
- (void)cycleLevel
{
    DLLevel cur = [DiagLog levelForCategory:DLCatAuth];
    DLLevel next = (cur >= DLLevelDebug) ? DLLevelError : (DLLevel)(cur + 1);
    [DiagLog setLevelForAll:next];
    self.verdictLabel.textColor = [UIColor colorWithWhite:0.65 alpha:1.0];
    self.verdictLabel.text = [NSString stringWithFormat:@"로그 레벨 전체 → %@",
                              [DiagLog levelName:next]];
    [self refreshFooter];
}

- (void)showStatus
{
    self.verdictLabel.textColor = [UIColor colorWithWhite:0.8 alpha:1.0];
    self.verdictLabel.text = [DiagLog statusSummary];
    [self refreshFooter];
}

@end
