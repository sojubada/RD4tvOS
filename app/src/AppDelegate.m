#import "AppDelegate.h"

#import "DiagLog.h"
#import "DiagViewController.h"

@implementation AppDelegate

- (BOOL)application:(UIApplication *)application
    didFinishLaunchingWithOptions:(NSDictionary *)launchOptions
{
    [DiagLog start];
    [DiagLog newTraceId];
    DLogI(DLCatLifecycle, @"앱 기동 bundle=%@ ver=%@",
          [[NSBundle mainBundle] bundleIdentifier],
          [[NSBundle mainBundle] objectForInfoDictionaryKey:@"CFBundleShortVersionString"]);

    self.window = [[UIWindow alloc] initWithFrame:[UIScreen mainScreen].bounds];
    self.window.rootViewController = [DiagViewController new];
    [self.window makeKeyAndVisible];
    return YES;
}

- (void)applicationWillTerminate:(UIApplication *)application
{
    DLogI(DLCatLifecycle, @"앱 종료");
}

@end
