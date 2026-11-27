#import "KeyboardProbe.h"
#import "DiagLog.h"

#import <GameController/GameController.h>

@implementation KeyboardProbe

+ (instancetype)shared
{
    static KeyboardProbe *s;
    static dispatch_once_t once;
    dispatch_once(&once, ^{ s = [KeyboardProbe new]; });
    return s;
}

- (instancetype)init
{
    self = [super init];
    if (self) {
        _lastKeyDescription = @"(없음)";
    }
    return self;
}

- (void)start
{
    // GCKeyboard 는 tvOS 14 부터다. 심볼이 없는 런타임에서도 죽지 않게 클래스 존재를 먼저 본다.
    Class kbClass = NSClassFromString(@"GCKeyboard");
    if (!kbClass) {
        DLogE(DLCatInput, @"gamecontroller", -1,
              @"GCKeyboard 클래스가 런타임에 없다 — 이 tvOS 에서 원시 키 경로를 쓸 수 없다");
        return;
    }
    DLogI(DLCatInput, @"keyboard probe start, GCKeyboard 클래스 있음");

    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(onConnect:)
                                                 name:GCKeyboardDidConnectNotification
                                               object:nil];
    [[NSNotificationCenter defaultCenter] addObserver:self
                                             selector:@selector(onDisconnect:)
                                                 name:GCKeyboardDidDisconnectNotification
                                               object:nil];

    GCKeyboard *kb = [GCKeyboard coalescedKeyboard];
    if (kb) {
        [self attach:kb];
    } else {
        DLogI(DLCatInput, @"coalescedKeyboard 가 아직 nil — 연결 알림을 기다린다");
    }
}

- (void)onConnect:(NSNotification *)n
{
    GCKeyboard *kb = (GCKeyboard *)n.object;
    DLogI(DLCatInput, @"keyboard 연결 알림 수신");
    [self attach:kb];
}

- (void)onDisconnect:(NSNotification *)n
{
    _keyboardConnected = NO;
    DLogW(DLCatInput, @"keyboard 연결 해제");
    if (self.onChange) self.onChange();
}

- (void)attach:(GCKeyboard *)kb
{
    GCKeyboardInput *input = kb.keyboardInput;
    if (!input) {
        DLogE(DLCatInput, @"gamecontroller", -2, @"keyboardInput 이 nil 이다");
        return;
    }
    _keyboardConnected = YES;

    __weak KeyboardProbe *weakSelf = self;
    input.keyChangedHandler = ^(GCKeyboardInput *kbInput, GCControllerButtonInput *key,
                                GCKeyCode keyCode, BOOL pressed) {
        KeyboardProbe *s = weakSelf;
        if (!s) return;
        if (pressed) s->_keyDownCount++; else s->_keyUpCount++;

        NSString *name = key.unmappedLocalizedName ?: key.localizedName ?: @"?";
        s->_lastKeyDescription = [NSString stringWithFormat:@"%@ (code=%ld) %@",
                                  name, (long)keyCode, pressed ? @"down" : @"up"];

        // 어느 입력이 들어와 무엇으로 변환됐는지 숫자로 남긴다.
        DLogI(DLCatInput, @"key %@ code=%ld pressed=%d down=%lu up=%lu",
              name, (long)keyCode, (int)pressed,
              (unsigned long)s->_keyDownCount, (unsigned long)s->_keyUpCount);

        if (s.onChange) {
            dispatch_async(dispatch_get_main_queue(), ^{ if (s.onChange) s.onChange(); });
        }
    };

    DLogI(DLCatInput, @"keyChangedHandler 등록 완료 — 이제 키를 누르면 로그가 남는다");
    if (self.onChange) self.onChange();
}

- (NSString *)statusLine
{
    if (!NSClassFromString(@"GCKeyboard")) {
        return @"키보드: GCKeyboard 없음 (이 tvOS 에서 원시 키 경로 불가)";
    }
    if (!_keyboardConnected) {
        return @"키보드: 미연결 — 블루투스 키보드를 연결하고 키를 눌러 보십시오";
    }
    return [NSString stringWithFormat:@"키보드: 연결됨 · down %lu / up %lu · 마지막 %@",
            (unsigned long)_keyDownCount, (unsigned long)_keyUpCount, _lastKeyDescription];
}

@end
