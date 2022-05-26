int _fltused = 1;

unsigned int str_cat(char* dst, char* src) {
	char* c = dst;
	for (; *c; c++);
	for (char* s = src; *c = *s; s++, c++);
	return (int)(c - dst);
}

unsigned int str_vacat(char* dst, int argc, ...) {
	char* c = dst;
	va_list ap;
	va_start(ap, argc);
	for (int i = 0; i < argc; i++)
		c += str_cat(c, va_arg(ap, char*));
	va_end(ap);
	return (int)(c - dst);
}

char* strstr(const char* str1, const char* str2) {
	char* c1 = (char*)str1, * c2 = (char*)str2;
	for (; *c1 && *c2; c2 = (*c1 == *c2 ? c2 + 1 : str2), c1++);
	return *c2 ? 0 : c1 - (c2 - str2);
}

#pragma function(memcpy)
void* memcpy(void* dst, void* src, size_t num) {
	while (num--)
		*((unsigned char*)dst)++ = *((unsigned char*)src)++;
	return dst;
}

void* memcpy_r(void* dst, void* src, size_t num) {
	while (num--)
		*((unsigned char*)dst + num) = *((unsigned char*)src + num);
	return dst;
}


#pragma function(memset)
void* memset(void* ptr, int value, size_t num) {
	while (num--)
		((unsigned char*)ptr)[num] = (unsigned char)value;
	return ptr;
}

#pragma function(memcmp)
int memcmp(const void* ptr1, const void* ptr2, size_t num) {
	int diff = 0;
	for (int i = 0; i < num && diff == 0; i++) {
		diff = ((unsigned char*)ptr1)[i] - ((unsigned char*)ptr2)[i];
	}
	return diff;
}

#pragma function(memmove)
void* memmove(void* dst, void* src, size_t num) {
	return dst > src ? memcpy_r(dst, src, num) : memcpy(dst, src, num);
}

#pragma function(strcmp)
int strcmp(const char* str1, const char* str2) {
	int diff = 0;
	for (; !(diff = *str1 - *str2) && *str1 && *str2; str1++, str2++);
	return diff;
}

#pragma function(strlen)
size_t strlen(const char* str) {
	size_t len = 0;
	while (*(str++))
		len++;
	return len;
}

void* malloc(size_t size) {
	return VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void free(void* ptr) {
	VirtualFree(ptr, 0, MEM_RELEASE);
}

char* strchr(const char* str, int character) {
	for (; *str && *str != (char)character; str++);
	return (*str == (char)character) ? (char*)str : 0;
}

int ipow(int base, int exponent) {
	if (!exponent)
		return 1;
	int res = base;
	while (exponent-- > 1)
		res *= base;
	return res;
}

int str_getint(char* str) {
	int sign = 1;
	if (*str == '-') {
		sign = -1;
		str++;
	}
	unsigned char* digit = str;
	for (; *digit >= '0' && *digit <= '9'; digit++);
	digit--;
	int total = 0;
	for (int i = 0; digit >= str; i++, digit--)
		total += (*digit - '0') * ipow(10, i);
	return total * sign;
}

float str_getfloat(char* str) {
	float sign = 1;
	if (*str == '-') {
		sign = -1;
		str++;
	}
	float a = (float)str_getint(str);
	char* point = strchr(str, '.');
	if (point) {
		point++;
		float b = (float)str_getint(point);
		int digitCount = 0;
		for (; *point >= '0' && *point <= '9'; point++)
			digitCount++;
		b = b / (float)ipow(10, digitCount);
		return (a + b) * sign;
	}
	return a;
}

int str_gethex(char* str, int* digitCount) {
	char lookup[] = { -1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,1,2,3,4,5,6,7,8,9,-1,-1,-1,-1,-1,-1,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1,-1,-1,10,11,12,13,14,15,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,0,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1 };
	unsigned char* digit = str;
	for (; lookup[*digit] >= 0; digit++);
	if (digitCount)
		*digitCount = (int)(digit - str);
	digit--;
	int total = 0;
	for (int i = 0; digit >= str; i++, digit--)
		total += lookup[*digit] * ipow(16, i);
	return total;
}

int uint_to_str(unsigned int num, char* str) {
	char tmp[16] = { 0 };
	int i = 0;
	for (; num > 0; i++) {
		int b = num % ipow(10, i + 1);
		tmp[14 - i] = '0' + b / ipow(10, i);
		num -= b;
	}
	if (str)
		memcpy(str, tmp + 15 - i, i + 1);
	return i;
}

void strrep(char* str, char* find, char* replace) {
	int findLen = (int)strlen(find);
	int repLen = (int)strlen(replace);
	for (char* found = str; found = strstr(found, find);) {
		memmove(found + findLen + (repLen - findLen), found + findLen, strlen(found + findLen) + 1);
		memcpy(found, replace, repLen);
		found += repLen;
	}
}

char* lowercase(char* str) {
	for (char* c = str; *c; c++)
		if (*c >= 'A' && *c <= 'Z')
			*c += 32;
	return str;
}