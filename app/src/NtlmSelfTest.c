#include "NtlmSelfTest.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include <winpr/ntlm.h>
#include <winpr/sspi.h>
#include <winpr/winpr.h>

// winpr 상류 테스트와 동일한 공개 벡터. 코드가 아니라 상수다.
static const char K_USER[] = "Username";
static const char K_DOMAIN[] = "Domain";
static const char K_PASSWORD[] = "P4ss123!";

static const BYTE K_NTLM_HASH[16] = { 0xd5, 0x92, 0x2a, 0x65, 0xc4, 0xd5, 0xc0, 0x82,
                                      0xca, 0x44, 0x4a, 0xf1, 0xbe, 0x00, 0x01, 0xdb };

static const BYTE K_NTLM_V2_HASH[16] = { 0x4c, 0x7f, 0x70, 0x6f, 0x7d, 0xde, 0x05, 0xa9,
                                         0xd1, 0xa0, 0xf4, 0xe7, 0xff, 0xe3, 0xbf, 0xb8 };

static const BYTE K_EMPTY_PWD_HASH[16] = { 0x31, 0xd6, 0xcf, 0xe0, 0xd1, 0x6a, 0xe9, 0x31,
                                           0xb7, 0x3c, 0x59, 0xd7, 0xe0, 0xc0, 0x89, 0xc0 };

static const BYTE K_EMPTY_PWD_V2_HASH[16] = { 0x0b, 0xce, 0x54, 0x87, 0x4e, 0x94, 0x20, 0x9e,
                                              0x34, 0x48, 0x97, 0xc1, 0x60, 0x03, 0x6e, 0x3b };

const char *NtlmSelfTest_VerdictName(NtlmTestVerdict v)
{
    switch (v)
    {
        case NTLM_TEST_PASS: return "PASS";
        case NTLM_TEST_FAIL: return "FAIL";
        default: return "SKIP";
    }
}

static unsigned long now_us(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (unsigned long)tv.tv_sec * 1000000UL + (unsigned long)tv.tv_usec;
}

static void hex16(const BYTE *b, char *out, size_t outlen)
{
    static const char *d = "0123456789abcdef";
    size_t n = 0;
    for (int i = 0; i < 16 && n + 2 < outlen; i++)
    {
        out[n++] = d[(b[i] >> 4) & 0x0F];
        out[n++] = d[b[i] & 0x0F];
    }
    out[n] = '\0';
}

typedef struct
{
    NtlmTestResult *arr;
    int             max;
    int             count;
} Collector;

static NtlmTestResult *begin(Collector *c, const char *name)
{
    if (c->count >= c->max)
        return NULL;
    NtlmTestResult *r = &c->arr[c->count++];
    memset(r, 0, sizeof(*r));
    strncpy(r->name, name, sizeof(r->name) - 1);
    r->verdict = NTLM_TEST_SKIP;
    r->elapsed_us = now_us();
    return r;
}

static void finish(NtlmTestResult *r, NtlmTestVerdict v, const char *fmt, ...)
{
    if (!r)
        return;
    r->verdict = v;
    r->elapsed_us = now_us() - r->elapsed_us;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(r->detail, sizeof(r->detail), fmt, ap);
    va_end(ap);
}

// ---------------------------------------------------------------------------
// 기지 정답 해시 시험
//
// NTOWFv1A 의 PasswordLengthInBytes 가 바이트 길이인지 0(자동)인지 헤더만으로는
// 단정할 수 없다. 추측해서 틀린 결론을 내는 대신 두 규약을 모두 시험하고
// 어느 쪽이 맞았는지를 기록한다 — 모호함을 데이터로 바꾼다.
// ---------------------------------------------------------------------------
static int ntowf_v1_try(const char *pwd, BYTE *out, const char **how)
{
    size_t len = strlen(pwd);
    memset(out, 0, 16);
    if (NTOWFv1A(pwd, (UINT32)len, out))
    {
        *how = "len=strlen";
        return 1;
    }
    memset(out, 0, 16);
    if (NTOWFv1A(pwd, 0, out))
    {
        *how = "len=0";
        return 1;
    }
    *how = "둘 다 실패";
    return 0;
}

static void test_ntowf_v1(Collector *c)
{
    NtlmTestResult *r = begin(c, "T1 NTOWFv1 기지정답 (MD4)");
    BYTE h[16];
    const char *how = "";
    char got[40], want[40];

    if (!ntowf_v1_try(K_PASSWORD, h, &how))
    {
        finish(r, NTLM_TEST_FAIL,
               "NTOWFv1A 가 FALSE 를 반환했다 (%s). MD4 를 가져오지 못한 것이다 — "
               "WITH_INTERNAL_MD4 가 꺼진 빌드이거나 OpenSSL legacy 프로바이더가 없다.",
               how);
        return;
    }
    hex16(h, got, sizeof(got));
    hex16(K_NTLM_HASH, want, sizeof(want));
    if (memcmp(h, K_NTLM_HASH, 16) == 0)
        finish(r, NTLM_TEST_PASS, "%s, hash=%s (%s)", how, got, "기대값 일치");
    else
        finish(r, NTLM_TEST_FAIL, "%s, got=%s want=%s — 계산은 됐지만 값이 틀렸다", how, got,
               want);
}

static void test_ntowf_v1_empty(Collector *c)
{
    NtlmTestResult *r = begin(c, "T2 NTOWFv1 빈 비밀번호");
    BYTE h[16];
    const char *how = "";
    char got[40];

    if (!ntowf_v1_try("", h, &how))
    {
        finish(r, NTLM_TEST_FAIL, "NTOWFv1A 실패 (%s)", how);
        return;
    }
    hex16(h, got, sizeof(got));
    if (memcmp(h, K_EMPTY_PWD_HASH, 16) == 0)
        finish(r, NTLM_TEST_PASS, "%s, hash=%s", how, got);
    else
        finish(r, NTLM_TEST_FAIL, "%s, got=%s (기대 31d6cfe0...)", how, got);
}

static void test_ntowf_v2(Collector *c)
{
    NtlmTestResult *r = begin(c, "T3 NTOWFv2 기지정답 (HMAC-MD5)");
    BYTE h[16];
    char got[40], want[40];
    BOOL ok = NTOWFv2A(K_PASSWORD, (UINT32)strlen(K_PASSWORD), K_USER, (UINT32)strlen(K_USER),
                       K_DOMAIN, (UINT32)strlen(K_DOMAIN), h);
    if (!ok)
    {
        memset(h, 0, sizeof(h));
        ok = NTOWFv2A(K_PASSWORD, 0, K_USER, 0, K_DOMAIN, 0, h);
    }
    if (!ok)
    {
        finish(r, NTLM_TEST_FAIL,
               "NTOWFv2A 가 FALSE. MD4 또는 HMAC-MD5 를 가져오지 못했다.");
        return;
    }
    hex16(h, got, sizeof(got));
    hex16(K_NTLM_V2_HASH, want, sizeof(want));
    if (memcmp(h, K_NTLM_V2_HASH, 16) == 0)
        finish(r, NTLM_TEST_PASS, "hash=%s", got);
    else
        finish(r, NTLM_TEST_FAIL, "got=%s want=%s", got, want);
}

static void test_ntowf_v2_empty(Collector *c)
{
    NtlmTestResult *r = begin(c, "T4 NTOWFv2 빈 비밀번호");
    BYTE h[16];
    char got[40];
    BOOL ok = NTOWFv2A("", 0, K_USER, (UINT32)strlen(K_USER), K_DOMAIN,
                       (UINT32)strlen(K_DOMAIN), h);
    if (!ok)
    {
        finish(r, NTLM_TEST_FAIL, "NTOWFv2A 실패");
        return;
    }
    hex16(h, got, sizeof(got));
    if (memcmp(h, K_EMPTY_PWD_V2_HASH, 16) == 0)
        finish(r, NTLM_TEST_PASS, "hash=%s", got);
    else
        finish(r, NTLM_TEST_FAIL, "got=%s (기대 0bce5487...)", got);
}

// ---------------------------------------------------------------------------
// SSPI 경로
// ---------------------------------------------------------------------------
static SecurityFunctionTableA *g_table = NULL;
static SecPkgInfoA            *g_pkg = NULL;

static void test_sspi_init(Collector *c)
{
    NtlmTestResult *r = begin(c, "T5 SSPI 초기화 + NTLM 패키지");
    char pkgname[] = "NTLM";

    sspi_GlobalInit();
    g_table = InitSecurityInterfaceExA(SSPI_INTERFACE_WINPR);
    if (!g_table)
    {
        finish(r, NTLM_TEST_FAIL, "InitSecurityInterfaceExA 가 NULL 을 반환했다");
        return;
    }
    if (!g_table->QuerySecurityPackageInfoA)
    {
        finish(r, NTLM_TEST_FAIL, "테이블에 QuerySecurityPackageInfoA 가 없다");
        return;
    }
    SECURITY_STATUS st = g_table->QuerySecurityPackageInfoA(pkgname, &g_pkg);
    if (st != SEC_E_OK || !g_pkg)
    {
        finish(r, NTLM_TEST_FAIL, "QuerySecurityPackageInfo status=0x%08X", (unsigned)st);
        return;
    }
    finish(r, NTLM_TEST_PASS, "dwVersion=%u cbMaxToken=%u", (unsigned)g_table->dwVersion,
           (unsigned)g_pkg->cbMaxToken);
}

static int token_msgtype(const SecBuffer *b)
{
    if (!b || !b->pvBuffer || b->cbBuffer < 12)
        return -1;
    const BYTE *p = (const BYTE *)b->pvBuffer;
    if (memcmp(p, "NTLMSSP\0", 8) != 0)
        return -2;
    return (int)p[8];
}

// 클라이언트와 서버를 같은 프로세스에 두고 Type1 → Type2 → Type3 을 주고받는다.
// 서버가 SEC_E_OK 를 반환하면 NTLMv2 응답 계산과 검증이 양쪽 다 맞은 것이다.
static void test_handshake(Collector *c)
{
    NtlmTestResult *r = begin(c, "T6 in-process NTLM 교환");
    char pkgname[] = "NTLM";
    char target[] = "RD4TVOS";

    if (!g_table || !g_pkg)
    {
        finish(r, NTLM_TEST_SKIP, "T5 가 실패해 건너뛴다");
        return;
    }

    SEC_WINNT_AUTH_IDENTITY cid, sid;
    memset(&cid, 0, sizeof(cid));
    memset(&sid, 0, sizeof(sid));
    CredHandle cCred, sCred;
    CtxtHandle cCtx, sCtx;
    TimeStamp exp;
    SECURITY_STATUS st;
    ULONG attrs = 0;
    BYTE *bufA = NULL, *bufB = NULL;
    const ULONG cap = g_pkg->cbMaxToken;
    int step = 0;
    char note[160] = "";
    SecBuffer sbA, sbB;
    SecBufferDesc descA, descB;

    memset(&sbA, 0, sizeof(sbA));
    memset(&sbB, 0, sizeof(sbB));
    memset(&descA, 0, sizeof(descA));
    memset(&descB, 0, sizeof(descB));

    SecInvalidateHandle(&cCred);
    SecInvalidateHandle(&sCred);
    SecInvalidateHandle(&cCtx);
    SecInvalidateHandle(&sCtx);

    if (sspi_SetAuthIdentityA(&cid, K_USER, K_DOMAIN, K_PASSWORD) < 0 ||
        sspi_SetAuthIdentityA(&sid, K_USER, K_DOMAIN, K_PASSWORD) < 0)
    {
        finish(r, NTLM_TEST_FAIL, "sspi_SetAuthIdentityA 실패");
        goto cleanup;
    }

    st = g_table->AcquireCredentialsHandleA(NULL, pkgname, SECPKG_CRED_OUTBOUND, NULL, &cid, NULL,
                                            NULL, &cCred, &exp);
    if (st != SEC_E_OK)
    {
        finish(r, NTLM_TEST_FAIL, "클라이언트 AcquireCredentialsHandle status=0x%08X",
               (unsigned)st);
        goto cleanup;
    }
    st = g_table->AcquireCredentialsHandleA(NULL, pkgname, SECPKG_CRED_INBOUND, NULL, &sid, NULL,
                                            NULL, &sCred, &exp);
    if (st != SEC_E_OK)
    {
        finish(r, NTLM_TEST_FAIL, "서버 AcquireCredentialsHandle status=0x%08X", (unsigned)st);
        goto cleanup;
    }

    bufA = (BYTE *)calloc(1, cap);
    bufB = (BYTE *)calloc(1, cap);
    if (!bufA || !bufB)
    {
        finish(r, NTLM_TEST_FAIL, "토큰 버퍼 할당 실패 (cbMaxToken=%u)", (unsigned)cap);
        goto cleanup;
    }

    sbA.cbBuffer = cap;
    sbA.BufferType = SECBUFFER_TOKEN;
    sbA.pvBuffer = bufA;
    sbB.cbBuffer = cap;
    sbB.BufferType = SECBUFFER_TOKEN;
    sbB.pvBuffer = bufB;
    descA.ulVersion = SECBUFFER_VERSION;
    descA.cBuffers = 1;
    descA.pBuffers = &sbA;
    descB.ulVersion = SECBUFFER_VERSION;
    descB.cBuffers = 1;
    descB.pBuffers = &sbB;

    // 1) 클라이언트 → NEGOTIATE (Type 1)
    step = 1;
    sbA.cbBuffer = cap;
    st = g_table->InitializeSecurityContextA(&cCred, NULL, target, ISC_REQ_CONFIDENTIALITY, 0,
                                             SECURITY_NATIVE_DREP, NULL, 0, &cCtx, &descA, &attrs,
                                             &exp);
    if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_OK)
    {
        finish(r, NTLM_TEST_FAIL, "step1 InitializeSecurityContext status=0x%08X", (unsigned)st);
        goto cleanup;
    }
    if (token_msgtype(&sbA) != 1)
    {
        finish(r, NTLM_TEST_FAIL, "step1 토큰이 NEGOTIATE 가 아니다 (msgtype=%d len=%u)",
               token_msgtype(&sbA), (unsigned)sbA.cbBuffer);
        goto cleanup;
    }
    snprintf(note, sizeof(note), "T1=%u", (unsigned)sbA.cbBuffer);

    // 2) 서버 → CHALLENGE (Type 2)
    step = 2;
    sbB.cbBuffer = cap;
    st = g_table->AcceptSecurityContext(&sCred, NULL, &descA, ASC_REQ_CONFIDENTIALITY,
                                        SECURITY_NATIVE_DREP, &sCtx, &descB, &attrs, &exp);
    if (st != SEC_I_CONTINUE_NEEDED && st != SEC_E_OK)
    {
        finish(r, NTLM_TEST_FAIL, "step2 AcceptSecurityContext status=0x%08X (%s)", (unsigned)st,
               note);
        goto cleanup;
    }
    if (token_msgtype(&sbB) != 2)
    {
        finish(r, NTLM_TEST_FAIL, "step2 토큰이 CHALLENGE 가 아니다 (msgtype=%d)",
               token_msgtype(&sbB));
        goto cleanup;
    }
    snprintf(note + strlen(note), sizeof(note) - strlen(note), " T2=%u",
             (unsigned)sbB.cbBuffer);

    // 3) 클라이언트 → AUTHENTICATE (Type 3). 여기서 NTLMv2 응답이 계산된다.
    step = 3;
    sbA.cbBuffer = cap;
    st = g_table->InitializeSecurityContextA(&cCred, &cCtx, target, ISC_REQ_CONFIDENTIALITY, 0,
                                             SECURITY_NATIVE_DREP, &descB, 0, &cCtx, &descA,
                                             &attrs, &exp);
    if (st != SEC_E_OK && st != SEC_I_CONTINUE_NEEDED && st != SEC_I_COMPLETE_NEEDED)
    {
        finish(r, NTLM_TEST_FAIL,
               "step3 InitializeSecurityContext status=0x%08X (%s) — NTLMv2 응답 계산 실패. "
               "MD4/RC4 를 못 가져온 경우 여기서 터진다.",
               (unsigned)st, note);
        goto cleanup;
    }
    if (token_msgtype(&sbA) != 3)
    {
        finish(r, NTLM_TEST_FAIL, "step3 토큰이 AUTHENTICATE 가 아니다 (msgtype=%d)",
               token_msgtype(&sbA));
        goto cleanup;
    }
    snprintf(note + strlen(note), sizeof(note) - strlen(note), " T3=%u",
             (unsigned)sbA.cbBuffer);

    // 4) 서버가 검증한다. SEC_E_OK 면 양쪽 계산이 일치한 것이다.
    step = 4;
    sbB.cbBuffer = cap;
    st = g_table->AcceptSecurityContext(&sCred, &sCtx, &descA, ASC_REQ_CONFIDENTIALITY,
                                        SECURITY_NATIVE_DREP, &sCtx, &descB, &attrs, &exp);
    if (st != SEC_E_OK)
    {
        finish(r, NTLM_TEST_FAIL, "step4 서버 검증 status=0x%08X (%s)", (unsigned)st, note);
        goto cleanup;
    }

    finish(r, NTLM_TEST_PASS, "4단계 완료, 서버 검증 SEC_E_OK (%s 바이트)", note);

cleanup:
    if (step > 0)
    {
        if (SecIsValidHandle(&cCtx) && g_table->DeleteSecurityContext)
            (void)g_table->DeleteSecurityContext(&cCtx);
        if (SecIsValidHandle(&sCtx) && g_table->DeleteSecurityContext)
            (void)g_table->DeleteSecurityContext(&sCtx);
        if (SecIsValidHandle(&cCred) && g_table->FreeCredentialsHandle)
            (void)g_table->FreeCredentialsHandle(&cCred);
        if (SecIsValidHandle(&sCred) && g_table->FreeCredentialsHandle)
            (void)g_table->FreeCredentialsHandle(&sCred);
    }
    free(bufA);
    free(bufB);
    sspi_FreeAuthIdentity(&cid);
    sspi_FreeAuthIdentity(&sid);
}

int NtlmSelfTest_Run(NtlmTestResult *results, int max, int *all_pass)
{
    Collector c = { results, max, 0 };

    test_ntowf_v1(&c);
    test_ntowf_v1_empty(&c);
    test_ntowf_v2(&c);
    test_ntowf_v2_empty(&c);
    test_sspi_init(&c);
    test_handshake(&c);

    if (g_pkg && g_table && g_table->FreeContextBuffer)
    {
        (void)g_table->FreeContextBuffer(g_pkg);
        g_pkg = NULL;
    }

    int pass = 1;
    for (int i = 0; i < c.count; i++)
    {
        if (c.arr[i].verdict == NTLM_TEST_FAIL)
            pass = 0;
    }
    if (all_pass)
        *all_pass = pass;
    return c.count;
}
