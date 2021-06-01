//https://www.intel.com/content/dam/doc/white-paper/advanced-encryption-standard-new-instructions-set-paper.pdf

#include <wmmintrin.h>

inline void KEY_256_ASSIST_1(__m128i* temp1, __m128i* temp2) {
    __m128i temp4;
    *temp2 = _mm_shuffle_epi32(*temp2, 0xff);
    temp4 = _mm_slli_si128(*temp1, 0x4);
    *temp1 = _mm_xor_si128(*temp1, temp4);
    temp4 = _mm_slli_si128(temp4, 0x4);
    *temp1 = _mm_xor_si128(*temp1, temp4);
    temp4 = _mm_slli_si128(temp4, 0x4);
    *temp1 = _mm_xor_si128(*temp1, temp4);
    *temp1 = _mm_xor_si128(*temp1, *temp2);
}

inline void KEY_256_ASSIST_2(__m128i* temp1, __m128i* temp3) {
    __m128i temp2, temp4;
    temp4 = _mm_aeskeygenassist_si128(*temp1, 0x0);
    temp2 = _mm_shuffle_epi32(temp4, 0xaa);
    temp4 = _mm_slli_si128(*temp3, 0x4);
    *temp3 = _mm_xor_si128(*temp3, temp4);
    temp4 = _mm_slli_si128(temp4, 0x4);
    *temp3 = _mm_xor_si128(*temp3, temp4);
    temp4 = _mm_slli_si128(temp4, 0x4);
    *temp3 = _mm_xor_si128(*temp3, temp4);
    *temp3 = _mm_xor_si128(*temp3, temp2);
}

#define KEY_256_ASSIST(Key_Schedule, ksIndex, rc, temp1, temp2, temp3) { \
    temp2 = _mm_aeskeygenassist_si128(temp3, rc); \
    KEY_256_ASSIST_1(&temp1, &temp2); \
    Key_Schedule[ksIndex] = temp1; \
    KEY_256_ASSIST_2(&temp1, &temp3); \
    Key_Schedule[ksIndex+1] = temp3; \
}

void AES_256_Key_Expansion(const unsigned char key[32], unsigned char keySchedule[240], unsigned char keyScheduleDecrypt[240]) {
    __m128i temp1, temp2, temp3;
    __m128i* Key_Schedule = (__m128i*)keySchedule;
    temp1 = _mm_loadu_si128((__m128i*)key);
    temp3 = _mm_loadu_si128((__m128i*)(key + 16));
    Key_Schedule[0] = temp1;
    Key_Schedule[1] = temp3;
    KEY_256_ASSIST(Key_Schedule, 2, 0x01, temp1, temp2, temp3);
    KEY_256_ASSIST(Key_Schedule, 4, 0x02, temp1, temp2, temp3);
    KEY_256_ASSIST(Key_Schedule, 6, 0x04, temp1, temp2, temp3);
    KEY_256_ASSIST(Key_Schedule, 8, 0x08, temp1, temp2, temp3);
    KEY_256_ASSIST(Key_Schedule, 10, 0x10, temp1, temp2, temp3);
    KEY_256_ASSIST(Key_Schedule, 12, 0x20, temp1, temp2, temp3);
    temp2 = _mm_aeskeygenassist_si128(temp3, 0x40);
    KEY_256_ASSIST_1(&temp1, &temp2);
    Key_Schedule[14] = temp1;
    ((__m128i*)keyScheduleDecrypt)[14] = Key_Schedule[0];
    for (int i = 1; i < 14; i++)
        ((__m128i*)keyScheduleDecrypt)[14 - i] = _mm_aesimc_si128(Key_Schedule[i]);
    ((__m128i*)keyScheduleDecrypt)[0] = Key_Schedule[14];
}

int AES_256_CBC_encrypt(unsigned char* buf, unsigned long length, unsigned char initVec[16], unsigned char keySchedule[240]) {
    __m128i feedback, data;
    unsigned int i, j;
    length /= 16;
    feedback = _mm_loadu_si128((__m128i*)initVec);
    for (i = 0; i < length; i++) {
        data = _mm_loadu_si128(&((__m128i*)buf)[i]);
        feedback = _mm_xor_si128(data, feedback);
        feedback = _mm_xor_si128(feedback, ((__m128i*)keySchedule)[0]);
        for (j = 1; j < 14; j++)
            feedback = _mm_aesenc_si128(feedback, ((__m128i*)keySchedule)[j]);
        feedback = _mm_aesenclast_si128(feedback, ((__m128i*)keySchedule)[j]);
        _mm_storeu_si128(&((__m128i*)buf)[i], feedback);
    }
    return 1;
}

int AES_256_CBC_decrypt(unsigned char* buf, unsigned long length, unsigned char initVec[16], unsigned char keySchedule[240]) {
    __m128i data, feedback, last_in;
    unsigned int i, j;
    length /= 16;
    feedback = _mm_loadu_si128((__m128i*)initVec);
    for (i = 0; i < length; i++) {
        last_in = _mm_loadu_si128(&((__m128i*)buf)[i]);
        data = _mm_xor_si128(last_in, ((__m128i*)keySchedule)[0]);
        for (j = 1; j < 14; j++)
            data = _mm_aesdec_si128(data, ((__m128i*)keySchedule)[j]);
        data = _mm_aesdeclast_si128(data, ((__m128i*)keySchedule)[j]);
        data = _mm_xor_si128(data, feedback);
        _mm_storeu_si128(&((__m128i*)buf)[i], data);
        feedback = last_in;
    }
    return 1;
}