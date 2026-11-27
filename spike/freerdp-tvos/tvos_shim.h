/* tvos_shim.h — FreeRDP tvOS 빌드용 강제 포함 헤더
 *
 * 벤더 소스를 고치지 않고 빌드를 통과시키기 위한 것이다(플레이북: 남의 코드는 읽기만).
 * CMAKE_C_FLAGS 의 -include 로 주입하므로 FreeRDP 트리에는 변경이 없다.
 *
 * 무엇을 막는가:
 *   winpr/libwinpr/thread/process.c 의 CreateProcess 구현이 fork(255행)와
 *   execve(426행)를 쓴다. tvOS SDK 는 이 둘을 __API_UNAVAILABLE(tvos) 로 표시한다.
 *
 * 왜 실패 스텁이 정답인가:
 *   RDP 클라이언트는 자식 프로세스를 만들지 않는다. fork() 가 -1 을 돌려주면
 *   process.c 는 그대로 `goto finish` 로 빠져 CreateProcess 가 실패를 반환한다.
 *   즉 tvOS 에서 "프로세스 생성은 지원되지 않는다"는 정확한 동작이 된다.
 *   거짓 성공을 만들지 않는다.
 *
 * 선언을 먼저 읽혀야 한다:
 *   이 헤더가 모든 TU 의 맨 앞에 들어가므로, 매크로를 정의하기 전에 unistd.h 를
 *   먼저 포함해야 한다. 그러지 않으면 시스템 헤더의 fork 선언 자체가 치환되어
 *   아래 정의와 충돌한다.
 */
#ifndef RD4TVOS_SPIKE_SHIM_H
#define RD4TVOS_SPIKE_SHIM_H

#if defined(__APPLE__)

#include <TargetConditionals.h>

#if defined(TARGET_OS_TV) && TARGET_OS_TV

#include <errno.h>
#include <sys/types.h>
#include <unistd.h>

static inline pid_t rd4tvos_fork_unavailable(void)
{
    errno = ENOSYS;
    return -1;
}

static inline int rd4tvos_execve_unavailable(const char *path, char *const argv[],
                                             char *const envp[])
{
    (void)path;
    (void)argv;
    (void)envp;
    errno = ENOSYS;
    return -1;
}

#define fork   rd4tvos_fork_unavailable
#define execve rd4tvos_execve_unavailable

#endif /* TARGET_OS_TV */
#endif /* __APPLE__ */

#endif /* RD4TVOS_SPIKE_SHIM_H */
