// KeyboardProbe.h — 블루투스 키보드의 키 이벤트가 앱에 들어오는지 확인한다
//
// 왜 지금 확인하는가: 원격 데스크탑에 키보드가 없으면 쓸 수 없으므로 포인터와 동급
// 항목이다. 그런데 tvOS 에서 GameController 의 키보드 경로가 실제로 동작하는지는
// 가정이 아니라 검증 대상이다. 1a 와 같은 릴리즈에 묶으면 릴리즈 하나를 아낀다.
//
// 시스템 텍스트 입력(UITextField)과 원시 키 이벤트는 다른 경로다. 원격 데스크탑이
// 필요한 것은 후자다 — 키를 누른 순간과 뗀 순간, 수정키 상태가 다 필요하다.
#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

@interface KeyboardProbe : NSObject

+ (instancetype)shared;

- (void)start;

@property (nonatomic, readonly) BOOL keyboardConnected;
@property (nonatomic, readonly) NSUInteger keyDownCount;
@property (nonatomic, readonly) NSUInteger keyUpCount;
@property (nonatomic, readonly, copy) NSString *lastKeyDescription;

// 상태가 바뀌면 호출된다. 화면 갱신용.
@property (nonatomic, copy, nullable) void (^onChange)(void);

- (NSString *)statusLine;

@end

NS_ASSUME_NONNULL_END
