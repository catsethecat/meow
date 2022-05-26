
typedef struct {
	char* key;
	char* value;
}inikeyvalue;

typedef struct {
	char* name;
	inikeyvalue* keyvalues;
}inisection;

typedef struct {
	char* data;
	inisection** sections;
}inifile;



/* inifile data member
sectionname1
key1
value1
key2
value2
sectionname2
key1
value1


# PTR name      |\
# PTR keyvalues | \ inisection
PTR k1
PTR v1
PTR k2
PTR v2
NULLPTR
# PTR name      |\
# PTR keyvalues | \ inisection
PTR k1
PTR v1
NULLPTR


PTR sect2
PTR sect1
NULLPTR
*/

inikeyvalue* iniGetSection(inifile* ini, char* sectionName) {
	for (inisection** section = ini->sections; *section; section++)
		if (strcmp((*section)->name, sectionName) == 0)
			return (*section)->keyvalues;
	return 0;
}

char* iniGetValue(inifile* ini, char* section, char* key) {
	inikeyvalue* kv = iniGetSection(ini, section);
	if (kv)
		for (; kv->key; kv++)
			if (strcmp(kv->key, key) == 0)
				return kv->value;
	return 0;
}

int iniSetValue(inifile* ini, char* section, char* key, char* value) {
	inikeyvalue* kv = iniGetSection(ini, section);
	if (kv)
		for (; kv->key; kv++)
			if (strcmp(kv->key, key) == 0) {
				kv->value = value;
				return 0;
			}
	return -1;
}

void utf16_to_utf8(char* str, int* newLen) {
	int offset = (*(unsigned short*)str == 0xFEFF);
	int i;
	for (i = 0; str[i] = str[offset + i * 2 + 1]; i++);
	if (newLen)
		*newLen = i;
}

inifile iniParse(char* filePath) {
    inifile ini = { 0 };

	HANDLE hFile;
	if ((hFile = CreateFileA(filePath, GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL)) == INVALID_HANDLE_VALUE)
		return ini;
	DWORD fileSize = GetFileSize(hFile, NULL);
	char* fileContents = malloc(fileSize+1);
	fileContents[fileSize] = 0;
	DWORD numRead = 0;
	ReadFile(hFile, fileContents, fileSize, &numRead, NULL);
	CloseHandle(hFile);

	if (*(unsigned short*)fileContents == 0xFEFF)
		utf16_to_utf8(fileContents, &fileSize);

	int lineCount = 0;
	for (char* c = fileContents; c = strchr(c+1, '\n'); lineCount++);

	int dataSize = fileSize + (lineCount + 1) * sizeof(char*) * 4;
	ini.data = malloc(dataSize);

	char** ptrs = (char**)(ini.data + fileSize);
	int ptrIndex = 0;
	char** sectionPtr = (char**)(ini.data + dataSize);
	sectionPtr--;
	sectionPtr[0] = 0;
	char* writePos = ini.data;

	char* line = fileContents;
	while (line) {
		char* nextLine = strchr(line, '\n');
		nextLine += nextLine != 0;
		int lineLen = nextLine ? (int)(nextLine - line) : fileSize - (int)(line - fileContents);
		//remove \r and \n from the end
		lineLen -= (line[lineLen - 2] == '\r') + (line[lineLen - 1] == '\n');
		line[lineLen] = 0;
		//remove comments
		char* comment = strchr(line, ';');
		if (comment) {
			*comment = 0;
			lineLen = (int)(comment - line);
		}
		//remove trailing spaces
		for (char* c = line + lineLen - 1; c >= line && *c == ' '; c--) {
			*c = 0;
			lineLen--;
		}
		//
		if (line[0] == '[') {
			line[lineLen - 1] = 0;
			memcpy(writePos, line + 1, lineLen - 1);
			ptrIndex++;
			ptrs[ptrIndex] = writePos;
			sectionPtr--;
			*sectionPtr = (char*)(&ptrs[ptrIndex]);
			ptrs[ptrIndex + 1] = (char*)(&ptrs[ptrIndex + 2]);
			ptrs[ptrIndex + 2] = 0;
			ptrIndex += 2;
			writePos += lineLen + 1;
		}
		else {
			char* separator = strchr(line, '=');
			if (separator) {
				char* value = separator + 1;
				for (; *value == ' '; value++);
				*separator = 0;
				for (char* c = separator - 1; c >= line && *c == ' '; c--, separator--)
					*c = 0;
				int keyLen = (int)(separator-line);
				int valueLen = lineLen - (int)(value-line);
				memcpy(writePos, line, keyLen + 1);
				ptrs[ptrIndex] = writePos;
				writePos += keyLen + 1;
				memcpy(writePos, value, valueLen + 1);
				ptrs[ptrIndex + 1] = writePos;
				ptrs[ptrIndex + 2] = 0;
				writePos += valueLen + 1;
				ptrIndex += 2;
			}
		}
		line = nextLine;
	}
	

	free(fileContents);

	ini.sections = (inisection**)sectionPtr;

    return ini;
}