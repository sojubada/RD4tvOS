// NtlmSelfTest.h — 1a 시험: tvOS 에서 NTLM 토큰이 만들어지는가
//
// 왜 이 시험이 먼저인가:
//   RDP 접속에서 가장 깨질 가능성이 높은 층이 NLA(CredSSP + NTLM)이고, 그 층은
//   서버 없이도 시험할 수 있다. 윈도우 PC 도, 네트워크도, 실제 비밀번호도 필요 없다.
//
// 무엇을 잡으려는가:
//   NTLM 은 MD4(NTOWF 해시)와 RC4(NTLMSSP 서명·봉인)를 쓴다. winpr 의 기본 경로는
//   OpenSSL 의 EVP_get_digestbyname("md4") 와 EVP_CIPHER_fetch("RC4") 인데,
//   OpenSSL 3.x 에서 둘 다 legacy 프로바이더 소속이라 로드하지 않으면 NULL 이 돌아온다.
//   컴파일은 통과하므로 이 결함은 기기에서 NLA 단계에만 터진다.
//   우리는 WITH_INTERNAL_MD4/RC4=ON 으로 내장 구현을 쓰도록 빌드했고, 이 시험이
//   그 전환이 실제로 통했는지를 판정한다.
//
// 기지 정답(known-answer)을 쓴다:
//   "토큰이 비어 있지 않다"로는 부족하다. 값이 맞는지를 본다. 벡터는 winpr 상류
//   테스트와 동일한 공개 값이며, 코드가 아니라 상수다.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    NTLM_TEST_PASS = 0,
    NTLM_TEST_FAIL = 1,
    NTLM_TEST_SKIP = 2
} NtlmTestVerdict;

typedef struct {
    char            name[64];     // 시험 이름
    NtlmTestVerdict verdict;
    char            detail[320];  // 숫자·상태코드가 들어간 한 줄. 추측을 사실로 바꾸는 부분
    unsigned long   elapsed_us;
} NtlmTestResult;

#define NTLM_SELFTEST_MAX 12

// results 배열에 결과를 채우고 개수를 반환한다. 전체 통과 여부는 *all_pass 로.
// 로깅은 호출자가 결과를 받아서 한다 — 이 파일은 순수 C 로 두어 winpr 외에
// 아무것도 의존하지 않게 한다.
int NtlmSelfTest_Run(NtlmTestResult *results, int max, int *all_pass);

const char *NtlmSelfTest_VerdictName(NtlmTestVerdict v);

#ifdef __cplusplus
}
#endif
