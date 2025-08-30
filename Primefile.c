#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <wchar.h>
#include <locale.h>
#include <errno.h>
#include <ctype.h>
#include <stdint.h>
#include <iconv.h>
#include <stdbool.h>
#include <fnmatch.h>
#include <libgen.h>
#include <sys/mman.h>
#include <sys/types.h>



static uint32_t g_next_handle = 1;
static char g_cwds[26][1024] = {0};
static char g_currentDrive = 'A';
static pthread_once_t g_init_flag = PTHREAD_ONCE_INIT;

// 初始化驱动器根目录
__attribute__((constructor)) static void ensure_prime_drive_roots_initialized() {
    for (int i = 0; i < 26; ++i) {
        snprintf(g_cwds[i], sizeof(g_cwds[i]), "prime_data/%c/", 'A' + i);
        mkdir(g_cwds[i], 0777);
    }
}
static char* map_path(const char *win_path) ;
bool ensure_parent_dirs_for_hostpath(const char* hostPath) {
    if (!hostPath || !*hostPath) return false;

    char* pathCopy = strdup(hostPath);
    if (!pathCopy) return false;

    char* parentDir = dirname(pathCopy);
    if (!parentDir) {
        free(pathCopy);
        return false;
    }

    struct stat st;
    if (stat(parentDir, &st) == 0) {
        free(pathCopy);
        return S_ISDIR(st.st_mode);
    }

    char* path = strdup(parentDir);
    if (!path) {
        free(pathCopy);
        return false;
    }

    char* p = path;
    while (*p == '/') p++; // Skip leading slashes

    bool success = true;
    while (true) {
        p = strchr(p, '/');
        if (!p) break;

        *p = '\0';
        if (*path) {
            if (mkdir(path, 0755) != 0 && errno != EEXIST) {
                success = false;
                break;
            }
        }
        *p++ = '/';
    }

    if (success && mkdir(parentDir, 0755) != 0 && errno != EEXIST) {
        success = false;
    }

    free(path);
    free(pathCopy);
    return success;
}	

// Device table entry structure
typedef struct vdev_entry {
    uint32_t handle;
    char* name;
    struct vdev_entry* next;
} vdev_entry_t;

static vdev_entry_t* g_vdev_table = NULL;
static uint32_t g_next_dev_handle = 1;

// Helper function to check string ending
static int ends_with(const char* str, const char* suffix) {
    size_t str_len = strlen(str);
    size_t suffix_len = strlen(suffix);
    return str_len >= suffix_len && 
           memcmp(str + str_len - suffix_len, suffix, suffix_len) == 0;
}

// Hex dump utility function
static void hex_dump(const void* data, size_t size) {
    const uint8_t* p = (const uint8_t*)data;
    for (size_t i = 0; i < size; i += 16) {
        printf("%04zx  ", i);
        for (size_t j = 0; j < 16 && i + j < size; ++j) {
            printf("%02x ", p[i + j]);
        }
        printf(" ");
        for (size_t j = 0; j < 16 && i + j < size; ++j) {
            char c = (char)p[i + j];
            printf("%c", isprint(c) ? c : '.');
        }
        printf("\n");
    }
}

uint32_t SDKLIB_CreateFile(const char* name) {
    
    printf("    +CreateFile_stub name:%s\n", name);

    // Create new entry
    vdev_entry_t* new_entry = malloc(sizeof(vdev_entry_t));
    //assert(new_entry != NULL);
    
    new_entry->handle = g_next_dev_handle++;
    new_entry->name = strdup(name);
    new_entry->next = g_vdev_table;
    g_vdev_table = new_entry;

    return new_entry->handle;
}

uint32_t SDKLIB_DeviceIoControl(uint32_t handle,uint32_t request,char* in,uint32_t size,char* out,int outlen,uint32_t* retlen,void* overlapped) {


    // Find the device in table
    vdev_entry_t* current = g_vdev_table;
    while (current != NULL) {
        if (current->handle == handle) break;
        current = current->next;
    }

    if (current == NULL) {
        fprintf(stderr, "    +DeviceIoControl_stub: Invalid handle %u\n", handle);
        return 0;
    }

    if (ends_with(current->name, "BAT")) {
        float* voltage = (float*)out;
        for (int i = 0; i < 4; ++i) voltage[i] = 4.0f;
        return 1;
    }
    if (ends_with(current->name, "ARCH")) {
        ((uint32_t*)out)[0]=0xAAAAAAAA;
        return 1;
    }

    printf("    +DeviceIoControl_stub file:%s request:%u sizein:%u sizeo:%d\n",
           current->name, request, size,outlen);
    
    memset(out, 0xff, outlen);
    return 1;
}

uint32_t SDKLIB_CloseHandle(uint32_t handle) {
    
    if (handle == 0) return 0;

    printf("    +CloseHandle_stub handle:%u\n", handle);

    vdev_entry_t** current = &g_vdev_table;
    while (*current != NULL) {
        if ((*current)->handle == handle) {
            vdev_entry_t* to_free = *current;
            *current = (*current)->next;
            free(to_free->name);
            free(to_free);
            return 1;
        }
        current = &(*current)->next;
    }
    return 0;
}
//--------------INI--------------------
// ====== INI Functions ======

// Helper function to trim whitespace
static char* trim_whitespace(char* str) {
    if (!str) return NULL;
    char *end;
    while (isspace((unsigned char)*str)) str++;
    if (*str == 0) return str;
    end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    *(end + 1) = 0;
    return str;
}

uint32_t SDKLIB__GetPrivateProfileString(const char* appName, const char* keyName, const char* def, char* outBuf, int size, const char* filename) {

    char* hostPath = filename ?  map_path(filename) : NULL;
    printf("GetPrivateProfileString\n");
    printf("    +appname: %s\n    +keyName: %s\n    +default: %s\n    +size: %i\n    +VM filename: %s\n    +Mapped host path: %s\n",
           appName ? appName : "(null)",
           keyName ? keyName : "(null)",
           def ? def : "(null)",
           size,
           filename ? filename : "(null)",
           hostPath ? hostPath : "(null)");

    if (!hostPath) {
        if (def && outBuf && size > 0) {
            strncpy(outBuf, def, size);
            return (uint32_t)strlen(def);
        }
        return 0;
    }

    FILE* f = fopen(hostPath, "rb");
    if (!f) {
        free(hostPath);
        if (def && outBuf && size > 0) {
            strncpy(outBuf, def, size);
            return (uint32_t)strlen(def);
        }
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long flen = ftell(f);
    fseek(f, 0, SEEK_SET);
    
    char* content = malloc(flen + 1);
    if (!content) {
        fclose(f);
        free(hostPath);
        return 0;
    }
    
    fread(content, 1, flen, f);
    content[flen] = '\0';
    fclose(f);
    free(hostPath);

    char* current_section = NULL;
    char* line = strtok(content, "\r\n");
    char* found_value = NULL;

    while (line) {
        char* trimmed = trim_whitespace(line);
        size_t len = strlen(trimmed);
        
        if (len > 0 && trimmed[0] != ';' && trimmed[0] != '#') {
            if (trimmed[0] == '[' && trimmed[len-1] == ']') {
                free(current_section);
                current_section = strndup(trimmed + 1, len - 2);
                trim_whitespace(current_section);
            } else if (current_section && appName && strcmp(current_section, appName) == 0) {
                char* eq = strchr(trimmed, '=');
                if (eq) {
                    *eq = '\0';
                    char* key = trim_whitespace(trimmed);
                    char* value = trim_whitespace(eq + 1);
                    
                    if (keyName && strcmp(key, keyName) == 0) {
                        found_value = strdup(value);
                        break;
                    }
                }
            }
        }
        line = strtok(NULL, "\r\n");
    }

    free(current_section);
    free(content);

    const char* result = found_value ? found_value : (def ? def : "");
    uint32_t result_len = strlen(result);
    
    if (outBuf && size > 0) {
        strncpy(outBuf, result, size);
        outBuf[size-1] = '\0';
    }
    
    free(found_value);
    return result_len;
}

uint32_t SDKLIB__WritePrivateProfileString(const char* appName, const char* keyName, const char* stringToWrite, const char* filename)   {

    char* hostPath = filename ?  map_path(filename) : NULL;
    
    printf("    +appname: %s\n    +keyName: %s\n    +string: %s\n    +VM filename: %s\n    +Mapped host path: %s\n",
           appName ? appName : "(null)",
           keyName ? keyName : "(null)",
           stringToWrite ? stringToWrite : "(null)",
           filename ? filename : "(null)",
           hostPath ? hostPath : "(null)");

    if (!hostPath || !appName) {
        free(hostPath);
        return 0;
    }

    // Ensure parent directories exist
    ensure_parent_dirs_for_hostpath(hostPath);
    
    // Read existing content or create new file
    char** lines = NULL;
    size_t line_count = 0;
    FILE* f = fopen(hostPath, "rb");
    
    if (f) {
        fseek(f, 0, SEEK_END);
        long flen = ftell(f);
        fseek(f, 0, SEEK_SET);
        
        if (flen > 0) {
            char* content = malloc(flen + 1);
            if (content) {
                fread(content, 1, flen, f);
                content[flen] = '\0';
                
                // Parse lines
                char* line = strtok(content, "\n");
                while (line) {
                    // Remove trailing carriage return if present
                    if (line[strlen(line)-1] == '\r') {
                        line[strlen(line)-1] = '\0';
                    }
                    
                    lines = realloc(lines, sizeof(char*) * (line_count + 1));
                    lines[line_count++] = strdup(line);
                    line = strtok(NULL, "\n");
                }
                free(content);
            }
        }
        fclose(f);
    }

    int section_start = -1, section_end = -1, key_line = -1;
    bool in_section = false;

    // Find section and key
    for (int i = 0; i < line_count; i++) {
        char* trimmed = trim_whitespace(lines[i]);
        if (strlen(trimmed) == 0) continue;

        if (trimmed[0] == '[') {
            in_section = false;
            char* end_bracket = strchr(trimmed, ']');
            if (end_bracket) {
                *end_bracket = '\0';
                char* section = trim_whitespace(trimmed + 1);
                if (strcmp(section, appName) == 0) {
                    section_start = i;
                    in_section = true;
                    section_end = i + 1;
                    
                    // Find end of section
                    for (int j = i + 1; j < line_count; j++) {
                        char* next_trimmed = trim_whitespace(lines[j]);
                        if (next_trimmed[0] == '[') {
                            section_end = j;
                            break;
                        }
                        if (j == line_count - 1) {
                            section_end = line_count;
                        }
                    }
                }
            }
        } else if (in_section && keyName) {
            char* eq = strchr(trimmed, '=');
            if (eq) {
                *eq = '\0';
                char* key = trim_whitespace(trimmed);
                if (strcmp(key, keyName) == 0) {
                    key_line = i;
                    break;
                }
            }
        }
    }

    // Perform modifications
    if (!keyName && !stringToWrite) {
        // Delete entire section
        if (section_start != -1 && section_end != -1) {
            for (int i = section_start; i < section_end; i++) {
                free(lines[i]);
            }
            memmove(&lines[section_start], &lines[section_end], 
                   (line_count - section_end) * sizeof(char*));
            line_count -= (section_end - section_start);
        }
    } else if (keyName && !stringToWrite) {
        // Delete specific key
        if (key_line != -1) {
            free(lines[key_line]);
            memmove(&lines[key_line], &lines[key_line+1], 
                   (line_count - key_line - 1) * sizeof(char*));
            line_count--;
        }
    } else if (keyName && stringToWrite) {
        char* new_line = malloc(strlen(keyName) + strlen(stringToWrite) + 2);
        sprintf(new_line, "%s=%s", keyName, stringToWrite);
        
        if (key_line != -1) {
            // Update existing key
            free(lines[key_line]);
            lines[key_line] = new_line;
        } else if (section_start != -1) {
            // Add to existing section
            lines = realloc(lines, sizeof(char*) * (line_count + 1));
            memmove(&lines[section_end+1], &lines[section_end], 
                   (line_count - section_end) * sizeof(char*));
            lines[section_end] = new_line;
            line_count++;
        } else {
            // Create new section
            lines = realloc(lines, sizeof(char*) * (line_count + 3));
            
            // Add blank line if not empty
            if (line_count > 0) {
                lines[line_count] = strdup("");
                line_count++;
            }
            
            // Add section header
            char* section_header = malloc(strlen(appName) + 3);
            sprintf(section_header, "[%s]", appName);
            lines[line_count] = section_header;
            line_count++;
            
            // Add key
            lines[line_count] = new_line;
            line_count++;
        }
    }

    // Write back to file
    f = fopen(hostPath, "wb");
    if (f) {
        for (int i = 0; i < line_count; i++) {
            fprintf(f, "%s\r\n", lines[i]);
            free(lines[i]);
        }
        fclose(f);
    } else {
        // Failed to create file
        for (int i = 0; i < line_count; i++) {
            free(lines[i]);
        }
        free(lines);
        free(hostPath);
        return 0;
    }

    free(lines);
    free(hostPath);
    return 1;
}
//-------------------------------------------
//#define VFS_ROOT "/home/kali/qemuPrime/build/firmware/PrimeU/prime_data"
typedef uint16_t UTF16; 
enum fs_attribute_e {
    /** Entry is read only. */
    ATTR_READONLY = 0x1,
    /** Entry is hidden. */
    ATTR_HIDDEN = 0x2,
    /** Entry is a system file/directory. */
    ATTR_SYSTEM = 0x4,
    /** Entry is a directory. */
    ATTR_DIR = 0x10,
    /** Entry is archived. */
    ATTR_ARCHIVE = 0x20,
    ATTR_DEVICE = 0x40,
    /** Entry does not have any other attribute. */
    ATTR_NONE = 0x80,
};
typedef struct {
    /**
     * @brief Current file descriptor.
     */
    void *unk0; // 0x0
    /**
     * @brief Next file descriptor.
     */
    void *unk4; // 0x4
    /** UTF-16-encoded long filename of the entry. */
    UTF16 *filename_lfn; // 0x8
    /** DOS 8.3 filename of the entry. */
    char *filename; // 0xc
    /**
     * @brief Seems to be a mirror of #filename
     * @see filename
     */
    char *filename2_alt; // 0x10
    /** Size of file. */
    size_t size; // 0x14
    /**
     * @brief Modify timestamp.
     * @see FIND_TS_YEAR
     * @see FIND_TS_MONTH
     * @see FIND_TS_DAY
     * @see FIND_TS_HOUR
     * @see FIND_TS_MINUTE
     * @see FIND_TS_SECOND
     */
    unsigned int mtime; // 0x18
    /**
     * @brief Create/birth timestamp.
     * @details The format of this field seems to be similar to mtime, but seems to be corrupted somehow.
     * @see mtime
     */
    unsigned int btime; // 0x1c
    /**
     * @brief Access timestamp.
     * @see mtime
     */
    unsigned int atime; // 0x20
    /** FAT filesystem file attribute mask. */
    unsigned char attrib_mask; // 0x24
    /** FAT filesystem file attributes. */
    unsigned char attrib; // 0x25
} find_context_t;

// Internal state for findfirst/findnext operations.
// We'll store this in the opaque pointers of find_context_t.
typedef struct {
    DIR *dir_handle;
    char *pattern;
    char *search_path;
} linux_find_state_t;

/**
 * @brief Initializes the virtual filesystem root directory.
 * @details Creates the root directory if it doesn't exist.
 */
void fs_init();
// --- Helper Functions ---

// Initializes the VFS root.
const char* VFS_ROOT = NULL;

__attribute__((constructor)) void fs_init() {
char cwd[PATH_MAX];
    
    // Get current working directory
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        // Calculate required memory
        size_t path_len = strlen(cwd) + strlen("/prime_data") + 1;
        
        // Allocate memory for the path
        char* path = malloc(path_len);
        if (path != NULL) {
            // Construct the full path
            snprintf(path, path_len, "%s/prime_data", cwd);
            
            // Assign to global constant pointer
            VFS_ROOT = path;
        }
    }
    struct stat st = {0};
    if (stat(VFS_ROOT, &st) == -1) {
        mkdir(VFS_ROOT, 0755);
        printf("Created VFS root at %s\n", VFS_ROOT);
    }
}

UTF16* utf8_to_utf16(const char *in) {
    if (!in) return calloc(1, sizeof(UTF16));

    const unsigned char *p = (const unsigned char*)in;
    size_t utf16_len = 0;

    // --- Pass 1: Calculate required length ---
    while (*p) {
        if (*p <= 0x7F) { // 1-byte ASCII
            p += 1;
        } else if ((*p & 0xE0) == 0xC0) { // 2-byte sequence
            p += 2;
        } else if ((*p & 0xF0) == 0xE0) { // 3-byte sequence
            p += 3;
        } else if ((*p & 0xF8) == 0xF0) { // 4-byte sequence (becomes a surrogate pair)
            p += 4;
            utf16_len++; // Extra length for the second surrogate
        } else { // Invalid byte
            p += 1;
        }
        utf16_len++;
    }

    // --- Allocation ---
    UTF16 *out = malloc((utf16_len + 1) * sizeof(UTF16));
    if (!out) return NULL;
    UTF16 *out_ptr = out;
    p = (const unsigned char*)in;

    // --- Pass 2: Perform conversion ---
    while (*p) {
        uint32_t codepoint = 0;
        int len = 0;

        // Decode UTF-8 to a Unicode codepoint
        if (*p <= 0x7F) {
            codepoint = *p; len = 1;
        } else if ((*p & 0xE0) == 0xC0) {
            codepoint = ((p[0] & 0x1F) << 6) | (p[1] & 0x3F); len = 2;
        } else if ((*p & 0xF0) == 0xE0) {
            codepoint = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); len = 3;
        } else if ((*p & 0xF8) == 0xF0) {
            codepoint = ((p[0] & 0x07) << 18) | ((p[1] & 0x3F) << 12) | ((p[2] & 0x3F) << 6) | (p[3] & 0x3F); len = 4;
        } else {
            codepoint = 0xFFFD; len = 1; // Replacement character
        }

        p += len;

        // Encode codepoint to UTF-16
        if (codepoint <= 0xFFFF) {
            *out_ptr++ = (UTF16)codepoint;
        } else {
            codepoint -= 0x10000;
            *out_ptr++ = (UTF16)(0xD800 + (codepoint >> 10));
            *out_ptr++ = (UTF16)(0xDC00 + (codepoint & 0x3FF));
        }
    }

    *out_ptr = 0; // Null terminator
    return out;
}


/**
 * @brief Converts a UTF-16LE string to a newly allocated UTF-8 string.
 *
 * This elegant implementation uses a two-pass approach:
 * 1. Calculate the exact required number of bytes for the UTF-8 string.
 * 2. Allocate the precise amount of memory and perform the conversion.
 *
 * @param in A null-terminated UTF-16LE string.
 * @return A newly allocated, null-terminated UTF-8 string, or NULL on error.
 *         The caller is responsible for freeing the returned memory.
 */
char* utf16_to_utf8(const UTF16 *in) {
    if (!in) return strdup("");

    const UTF16 *p = in;
    size_t utf8_bytes = 0;

    // --- Pass 1: Calculate required bytes ---
    while (*p) {
        uint32_t codepoint = *p;
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) { // Surrogate pair
            p++; // Skip low surrogate in length calculation
            utf8_bytes += 4;
        } else if (codepoint <= 0x7F) {
            utf8_bytes += 1;
        } else if (codepoint <= 0x7FF) {
            utf8_bytes += 2;
        } else { // 0x800 to 0xFFFF
            utf8_bytes += 3;
        }
        p++;
    }

    // --- Allocation ---
    char *out = malloc(utf8_bytes + 1);
    if (!out) return NULL;
    char *out_ptr = out;
    p = in;

    // --- Pass 2: Perform conversion ---
    while (*p) {
        uint32_t codepoint = *p++;
        if (codepoint >= 0xD800 && codepoint <= 0xDBFF) { // High surrogate
            uint32_t low_surrogate = *p++;
            codepoint = 0x10000 + (((codepoint - 0xD800) << 10) | (low_surrogate - 0xDC00));
        }

        // Encode codepoint to UTF-8
        if (codepoint <= 0x7F) {
            *out_ptr++ = (char)codepoint;
        } else if (codepoint <= 0x7FF) {
            *out_ptr++ = 0xC0 | (codepoint >> 6);
            *out_ptr++ = 0x80 | (codepoint & 0x3F);
        } else if (codepoint <= 0xFFFF) {
            *out_ptr++ = 0xE0 | (codepoint >> 12);
            *out_ptr++ = 0x80 | ((codepoint >> 6) & 0x3F);
            *out_ptr++ = 0x80 | (codepoint & 0x3F);
        } else {
            *out_ptr++ = 0xF0 | (codepoint >> 18);
            *out_ptr++ = 0x80 | ((codepoint >> 12) & 0x3F);
            *out_ptr++ = 0x80 | ((codepoint >> 6) & 0x3F);
            *out_ptr++ = 0x80 | (codepoint & 0x3F);
        }
    }

    *out_ptr = '\0'; // Null terminator
    return out;
}

// Maps a Windows-style path to a Linux path inside our VFS.
// The caller must free the returned string.
// The caller must free the returned string.
static char* map_path(const char *win_path) {
    if (!win_path) return NULL;

    // Calculate required buffer size
    size_t vfs_root_len = strlen(VFS_ROOT);
    size_t win_path_len = strlen(win_path);
    size_t buffer_size = vfs_root_len + win_path_len + 10; // Extra space for modifications
    char *linux_path = malloc(buffer_size);
    if (!linux_path) return NULL;

    const char *p = win_path;
    size_t offset = 0; // Track current position in linux_path

    // Handle \\?\ prefix
    if (strncmp(p, "\\\\?\\", 4) == 0) {
        p += 4;
    }

    // Handle UNC paths \\server\share -> /vfs/UNC/server/share
    if (strncmp(p, "\\\\", 2) == 0) {
        const char *unc_start = p + 2; // Skip initial \\/
        const char *slash = strchr(unc_start, '\\');
        if (slash) {
            const char *share_start = slash + 1;
            const char *share_slash = strchr(share_start, '\\');
            if (share_slash) {
                // Format: /vfs/UNC/server/share/rest
                offset += snprintf(linux_path + offset, buffer_size - offset, "%s/UNC/", VFS_ROOT);
                // Copy server name
                strncat(linux_path + offset, unc_start, slash - unc_start);
                offset += slash - unc_start;
                // Copy share name
                offset += snprintf(linux_path + offset, buffer_size - offset, "/");
                strncat(linux_path + offset, share_start, share_slash - share_start);
                offset += share_slash - share_start;
                p = share_slash; // Set p to the rest of the path
            } else {
                // Format: /vfs/UNC/server/share
                offset += snprintf(linux_path + offset, buffer_size - offset, "%s/UNC/", VFS_ROOT);
                strncat(linux_path + offset, unc_start, buffer_size - offset);
                offset += strlen(unc_start);
                p += strlen(p); // No rest path
            }
        } else {
            // Invalid UNC path, treat as relative
            linux_path[offset] = '\0';
        }
    }
    // Handle drive letters (e.g., C:\ or C:/)
    else if (win_path_len >= 2 && isalpha(p[0]) && p[1] == ':') {
        char drive_letter = toupper(p[0]);
        offset += snprintf(linux_path, buffer_size, "%s/%c", VFS_ROOT, drive_letter);
        p += 2; // Skip drive letter and colon
        // If next character is slash, skip it (root case)
        if (*p == '\\' || *p == '/') {
            p++;
        }
    }
    // Handle absolute paths starting with slash (root of current drive)
    else if (win_path_len >= 1 && (p[0] == '\\' || p[0] == '/')) {
        // Map to current drive root (assuming default drive, e.g., C)
        offset += snprintf(linux_path, buffer_size, "%s/C", VFS_ROOT);
        p++; // Skip the leading slash
    }
    // Relative path
    else {
        linux_path[offset] = '\0'; // Start empty
    }

    // Append the rest of the path
    if (*p) {
        // Add separator if needed (if not empty and last char isn't slash)
        if (offset > 0 && linux_path[offset - 1] != '/') {
            offset += snprintf(linux_path + offset, buffer_size - offset, "/");
        }
        strncpy(linux_path + offset, p, buffer_size - offset);
        offset += strlen(p);
    }

    // Convert all backslashes to forward slashes
    for (char *c = linux_path; *c; c++) {
        if (*c == '\\') *c = '/';
    }

    return linux_path;
}


// Generates a simple 8.3 filename from a long filename.
static void generate_8_3_name(const char *lfn, char *sfn) {
    char basename[256];
    char extension[256];
    const char *dot = strrchr(lfn, '.');

    if (dot && dot != lfn) {
        strncpy(basename, lfn, dot - lfn);
        basename[dot - lfn] = '\0';
        strcpy(extension, dot + 1);
    } else {
        strcpy(basename, lfn);
        extension[0] = '\0';
    }

    char sfn_base[9] = {0};
    char sfn_ext[4] = {0};
    int j = 0;

    // Base name
    for (int i = 0; basename[i] && j < 8; ++i) {
        char c = toupper(basename[i]);
        if (isalnum(c)) {
            sfn_base[j++] = c;
        }
    }
    if (strlen(basename) > 8) {
        sfn_base[6] = '~';
        sfn_base[7] = '1';
    }

    // Extension
    j = 0;
    for (int i = 0; extension[i] && j < 3; ++i) {
        char c = toupper(extension[i]);
        if (isalnum(c)) {
            sfn_ext[j++] = c;
        }
    }

    if (strlen(sfn_ext) > 0) {
        sprintf(sfn, "%-8s.%-3s", sfn_base, sfn_ext);
    } else {
        sprintf(sfn, "%-8s", sfn_base);
    }
    // Trim trailing spaces
    char *end = sfn + strlen(sfn) - 1;
    while (end > sfn && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
}

// Converts time_t to FAT timestamp format
static unsigned int time_t_to_fat_ts(time_t t) {
    struct tm *local = localtime(&t);
    if (!local) return 0;
    
    unsigned int year = (local->tm_year + 1900 - 1980);
    unsigned int month = local->tm_mon + 1;
    unsigned int day = local->tm_mday;
    unsigned int hour = local->tm_hour;
    unsigned int min = local->tm_min;
    unsigned int sec = local->tm_sec;

    return (year << 25) | (month << 21) | (day << 16) |
           (hour << 11) | (min << 5) | (sec / 2); // FAT stores seconds/2
}

// --- `_a*` (ANSI/8.3) Implementations ---

short SDKLIB__achdir(const char *path) {
    printf("	+change path to: %s\n", path);
    char *linux_path = map_path(path);
    if (!linux_path) {
        
        return -1;
    }
    int ret = chdir(linux_path);
    free(linux_path);
    return (ret == 0) ? 0 : -1;
}

int SDKLIB__amkdir(char *path) {
    //printf("	+mkdir: %s\n", path);
    char *linux_path = map_path(path);
    if (!linux_path) {
        return -1;
    }
    
    // Ensure parent directories exist.
    char *p = linux_path;
    if (p[0] == '/') p++;
    while ((p = strchr(p, '/'))) {
        *p = '\0';
        mkdir(linux_path, 0755);
        *p = '/';
        p++;
    }

    int ret = mkdir(linux_path, 0755);
    free(linux_path);
    return (ret == 0 || errno == EEXIST) ? 0 : -1;
}

int SDKLIB__armdir(char *path) {
    char *linux_path = map_path(path);
    if (!linux_path) {
        return -1;
    }
    int ret = rmdir(linux_path);
    free(linux_path);
    return (ret == 0) ? 0 : -1;
}

short SDKLIB__afindnext(find_context_t *ctx); // Forward declaration

short SDKLIB__afindfirst(const char *fnmatch_path, find_context_t *ctx, int attrib_mask) {
    //printf("DEBUG: SSDKLIB__afindfirst called with pattern: %s, attrib_mask: %d\n", fnmatch_path, attrib_mask);
        memset(ctx, 0, sizeof(find_context_t));
    // FIX: Correctly separate directory path from glob pattern
    char *win_path_copy = strdup(fnmatch_path);
    char *win_dir_part_str;
    char *pattern_str;
    
    char *last_slash = strrchr(win_path_copy, '\\');
    char *last_fwd_slash = strrchr(win_path_copy, '/');
    if (last_fwd_slash > last_slash) last_slash = last_fwd_slash;

    if (last_slash) {
        // Path has a directory component, e.g., "C:\DATA\*.*"
        pattern_str = strdup(last_slash + 1);
        *(last_slash + 1) = '\0'; // Truncate to get the directory part
        win_dir_part_str = win_path_copy;
    } else {
        // Path is just a pattern, e.g., "*.*"
        pattern_str = strdup(win_path_copy);
        win_dir_part_str = strdup("."); // Use current directory
    }

    char *search_path = map_path(win_dir_part_str);
    free(win_path_copy);
    if (win_dir_part_str[0] == '.') free(win_dir_part_str);

    if (!search_path) {
        free(pattern_str);
        return -1;
    }

    DIR *dir = opendir(search_path);
    if (!dir) {
        perror("opendir");
        free(pattern_str);
        free(search_path);
        return -1;
    }

    linux_find_state_t *state = malloc(sizeof(linux_find_state_t));
    state->dir_handle = dir;
    state->pattern = pattern_str;
    state->search_path = search_path;

    ctx->unk0 = state;
    ctx->unk4 = (void*)0xdeadbeef;
    

    // Find the first actual match
    return SDKLIB__afindnext(ctx);
}

short SDKLIB__afindnext(find_context_t *ctx) {
    //printf("DEBUG: SSDKLIB__afindnext called\n");
    linux_find_state_t *state = (linux_find_state_t *)ctx->unk0;
    if (!state || !state->dir_handle) {
        return -1;
    }

    struct dirent *entry;
    while ((entry = readdir(state->dir_handle)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
           
            continue;
        }
        
        // Match against pattern
        if (fnmatch(state->pattern, entry->d_name, FNM_PATHNAME) == 0) {
            
            
            // Use libgen.h to safely combine paths
            char full_entry_path[PATH_MAX];
            if (snprintf(full_entry_path, sizeof(full_entry_path), "%s/%s", state->search_path, entry->d_name) >= sizeof(full_entry_path)) {
            
                continue;
            }

            // Normalize path to handle any redundant slashes
            char resolved_path[PATH_MAX];
            if (realpath(full_entry_path, resolved_path) == NULL) {
                continue;
            }

            struct stat st;
            if (stat(resolved_path, &st) != 0) {
                continue;
            }

            unsigned char attrib = 0;
            if (S_ISDIR(st.st_mode)) attrib |= ATTR_DIR;
            if (!(st.st_mode & S_IWUSR)) attrib |= ATTR_READONLY;
            

            // Free previous results if any
            free(ctx->filename_lfn);
            free(ctx->filename);
            free(ctx->filename2_alt);

            // Populate context
            ctx->filename_lfn = utf8_to_utf16(entry->d_name);
            ctx->filename = strdup(entry->d_name);
            ctx->filename2_alt = strdup(entry->d_name);
            
            char sfn[13];
            generate_8_3_name(entry->d_name, sfn);
            // Overwrite `filename` with the generated 8.3 name
            free(ctx->filename);
            ctx->filename = strdup(sfn);
            
            ctx->size = st.st_size;
            ctx->mtime = time_t_to_fat_ts(st.st_mtime);
            ctx->atime = time_t_to_fat_ts(st.st_atime);
            ctx->btime = time_t_to_fat_ts(st.st_ctime); // ctime is closest to birth time
            ctx->attrib = attrib;

            return 0; // Found a match
            }
    }

    return -1; // No more matches
}

bool SDKLIB__aremove(const char *pathname) {
    char *linux_path = map_path(pathname);
    if (!linux_path) {
        return false;
    }
    int result = remove(linux_path);
    free(linux_path);
    return (result == 0); // remove() returns 0 on success
}
// --- `_w*` (UTF-16/LFN) Implementations ---

short SDKLIB__wchdir(const UTF16 *path) {
    char *utf8_path = utf16_to_utf8(path);
    if (!utf8_path) return -1;
    short ret = SDKLIB__achdir(utf8_path);
    free(utf8_path);
    return ret;
}

int SDKLIB__wmkdir(UTF16 *path) {
    char *utf8_path = utf16_to_utf8(path);
    if (!utf8_path) return -1;
    int ret = SDKLIB__amkdir(utf8_path);
    free(utf8_path);
    return ret;
}

int SDKLIB__wrmdir(UTF16 *path) {
    char *utf8_path = utf16_to_utf8(path);
    if (!utf8_path) return -1;
    int ret = SDKLIB__armdir(utf8_path);
    free(utf8_path);
    return ret;
}

short SDKLIB__wfindfirst(const UTF16 *fnmatch, find_context_t *ctx, int attrib_mask) {
    char *utf8_fnmatch = utf16_to_utf8(fnmatch);
    if (!utf8_fnmatch) return -1;
    short ret = SDKLIB__afindfirst(utf8_fnmatch, ctx, attrib_mask);
    free(utf8_fnmatch);
    return ret;
}

short SDKLIB__wfindnext(find_context_t *ctx) {
    // _wfindnext and _afindnext are interchangeable in this implementation
    // as the internal state is based on UTF-8 anyway.
    return SDKLIB__afindnext(ctx);
}
bool SDKLIB___wremove(const UTF16 *pathname) {
    char *utf8_path = utf16_to_utf8(pathname);
    if (!utf8_path) {
        return false;
    }
    bool result = SDKLIB__aremove(utf8_path);
    free(utf8_path);
    return result;
}
// --- Common Implementation ---

int SDKLIB__findclose(find_context_t *ctx) {
if(!ctx){
printf("ERROR: close null point,may cause mem leak!!!!!!\n");
return -1;
}
    if (ctx && ctx->unk0) {
        linux_find_state_t *state = (linux_find_state_t *)ctx->unk0;
        closedir(state->dir_handle);
        free(state->pattern);
        free(state->search_path);
        free(state);
        ctx->unk0 = NULL;
    }
    // Free the strings allocated in the context
    free(ctx->filename_lfn);
    free(ctx->filename);
    free(ctx->filename2_alt);
    memset(ctx, 0, sizeof(find_context_t));
    return 0;
}
//-------------------------------------------------
enum sys_seek_whence_e {
    /** Seek from the beginning of file. */
    _SYS_SEEK_SET = 0,
    /** Seek from current offset. */
    _SYS_SEEK_CUR,
    /** Seek from the end of file. */
    _SYS_SEEK_END,
};

// =========================================================
// === NEW IMPLEMENTATIONS FOR file.h FUNCTIONS          ===
// =========================================================



FILE *SDKLIB__afopen(const char *pathname, const char *mode) {
   
    char *linux_path = map_path(pathname);
    if (!linux_path) {
        return NULL;
    }
    
    FILE *fp = fopen(linux_path, mode);
    
    //printf("	+fopen called with pathname='%s', mode='%s',%p\n", pathname, mode,fp);
    free(linux_path);
    
    // We cast the standard FILE* to our opaque FILE*

    return (FILE *)fp;
}

FILE *SDKLIB___wfopen(const UTF16 *pathname, const UTF16 *mode) {
    
    char *utf8_path = utf16_to_utf8(pathname);
    char *utf8_mode = utf16_to_utf8(mode);

    if (!utf8_path || !utf8_mode) {
        free(utf8_path);
        free(utf8_mode);
        return NULL;
    }
    // Reuse the ANSI implementation
    FILE *fd = SDKLIB__afopen(utf8_path, utf8_mode);
    
    free(utf8_path);
    free(utf8_mode);
    
    return fd;
}

size_t SDKLIB__fread(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    
    // Cast our opaque pointer back to the underlying FILE*
    size_t result = fread(ptr, size, nmemb, (FILE *)stream);
    
    return result;
}

size_t SDKLIB__fwrite(const void *ptr, size_t size, size_t nmemb, FILE *stream) {
    
    size_t result = fwrite(ptr, size, nmemb, (FILE *)stream);
    
    return result;
}

int SDKLIB___fseek(FILE *stream, long offset, int whence) {
    int linux_whence;
    switch (whence) {
        case _SYS_SEEK_SET:
            linux_whence = SEEK_SET;
            break;
        case _SYS_SEEK_CUR:
            linux_whence = SEEK_CUR;
            break;
        case _SYS_SEEK_END:
            linux_whence = SEEK_END;
            break;
        default:
            return -1; // Invalid whence value
    }
    
    int result = fseek((FILE *)stream, offset, linux_whence);
    return result;
}

long _ftell(FILE *stream) {
    long result = ftell((FILE *)stream);
    return result;
}

int __fflush(FILE *stream) {
    int result = fflush((FILE *)stream);
    return result;
}

int SDKLIB__fclose(FILE *stream) {
    int result = fclose((FILE *)stream);
    return result;
}
//------------------------------------loader rel----
size_t SDKLIB__filesize(FILE *file) {
	
    if (file == NULL) {
        errno = EINVAL; // Invalid argument
        return (size_t)-1;
    }
    int fd = fileno(file);
    if (fd == -1) {
        return (size_t)-1;
    }
    struct stat stat_buf;
    if (fstat(fd, &stat_buf) == -1) {
        return (size_t)-1;
    }
   
    return (size_t)stat_buf.st_size;
}


// --- Constants based on the documentation ---
// These values are typical for DOS 8.3 limitations.

// Pathname: C:\DIR\SUBDIR\FILENAME.EXT (plus some extra for safety)
// The historic MAX_PATH limit is a good choice.
#define FNSPLIT_DOS_PATHNAME_MAX 260
// Drive: C: + null
#define FNSPLIT_DOS_DRIVE_MAX 3
// Dir: A reasonable limit for old DOS paths
#define FNSPLIT_DOS_DIRNAME_MAX 256
// Basename: 8 chars + null
#define FNSPLIT_DOS_BASENAME_MAX 9
// Suffix: . + 3 chars + null
#define FNSPLIT_DOS_SUFFIX_MAX 5

// --- Bitfield constants for the return values ---
// These are standard bitmask values.
#define FNSPLIT_DRIVE      1
#define FNSPLIT_DIR        2
#define FNSPLIT_FILENAME   4
#define FNSPLIT_SUFFIX     8

/**
 * @brief Split a DOS 8.3 pathname into parts.
 * @details DOS 8.3 counterpart of _wfnsplit().
 * @x_syscall_num `0x100c5`
 * @param[in] pathname Pathname to be split.
 * @param[out] drive Drive name, or NULL to omit this part.
 * Must be at least as long as ::FNSPLIT_DOS_DRIVE_MAX.
 * @param[out] dirname Directory, or NULL to omit this part.
 * Must be at least as long as ::FNSPLIT_DOS_DIRNAME_MAX.
 * @param[out] basename Basename without suffix, or NULL to omit this part.
 * Must be at least as long as ::FNSPLIT_DOS_BASENAME_MAX.
 * @param[out] suffix Suffix, or NULL to omit this part.
 * Must be at least as long as ::FNSPLIT_DOS_SUFFIX_MAX.
 * @return Bitfield that indicates which parts were being extracted successfully.
 * @see _wfnsplit Its LFN counterpart.
 */
int SDKLIB__afnsplit(const char *pathname, char *drive, char *dirname, char *basename, char *suffix) {
    printf("--- _afnsplit called ---\n");
    printf("  [in] pathname: \"%s\"\n", pathname ? pathname : "(null)");
    printf("  [out] drive:    %p\n", (void*)drive);
    printf("  [out] dirname:  %p\n", (void*)dirname);
    printf("  [out] basename: %p\n", (void*)basename);
    printf("  [out] suffix:   %p\n", (void*)suffix);

    if (!pathname) {
        return 0;
    }

    // Initialize output buffers if they are not NULL
    if (drive) *drive = '\0';
    if (dirname) *dirname = '\0';
    if (basename) *basename = '\0';
    if (suffix) *suffix = '\0';

    int result = 0;
    const char *p = pathname;
    const char *last_slash = NULL;
    const char *last_dot = NULL;
    const char *iter;

    // Find the last backslash/slash and the last dot
    for (iter = pathname; *iter; ++iter) {
        if (*iter == '\\' || *iter == '/') {
            last_slash = iter;
        } else if (*iter == '.') {
            last_dot = iter;
        }
    }

    // 1. Extract Drive
    if (p[0] && p[1] == ':') {
        if (drive) {
            strncpy(drive, p, 2);
            drive[2] = '\0';
        }
        result |= FNSPLIT_DRIVE;
        p += 2; // Move pointer past the drive
    }

    // 2. Extract Suffix and Basename
    if (last_dot && (!last_slash || last_dot > last_slash)) {
        // We have a suffix
        if (suffix) {
            strncpy(suffix, last_dot, FNSPLIT_DOS_SUFFIX_MAX - 1);
            suffix[FNSPLIT_DOS_SUFFIX_MAX - 1] = '\0';
        }
        result |= FNSPLIT_SUFFIX;
        
        if (basename) {
            const char* start = last_slash ? last_slash + 1 : p;
            size_t len = last_dot - start;
            if (len >= FNSPLIT_DOS_BASENAME_MAX) len = FNSPLIT_DOS_BASENAME_MAX - 1;
            strncpy(basename, start, len);
            basename[len] = '\0';
        }
        result |= FNSPLIT_FILENAME;
    } else {
        // No suffix, the whole filename is the basename
        if (basename) {
            const char* start = last_slash ? last_slash + 1 : p;
            strncpy(basename, start, FNSPLIT_DOS_BASENAME_MAX - 1);
            basename[FNSPLIT_DOS_BASENAME_MAX - 1] = '\0';
        }
        if (basename && *basename) {
            result |= FNSPLIT_FILENAME;
        }
    }

    // 3. Extract Directory
    if (last_slash) {
        if (dirname) {
            size_t len = (last_slash - p) + 1;
            if (len >= FNSPLIT_DOS_DIRNAME_MAX) len = FNSPLIT_DOS_DIRNAME_MAX - 1;
            strncpy(dirname, p, len);
            dirname[len] = '\0';
        }
        result |= FNSPLIT_DIR;
    }

    printf("  [debug] Extracted -> drive:\"%s\", dir:\"%s\", base:\"%s\", suffix:\"%s\"\n",
        drive ? drive : "N/A",
        dirname ? dirname : "N/A",
        basename ? basename : "N/A",
        suffix ? suffix : "N/A");
    printf("  [return] %d\n\n", result);

    return result;
}

/**
 * @brief Build an DOS 8.3 pathname from parts.
 * @details DOS 8.3 counterpart of _wfnmerge().
 * @x_syscall_num `0x100c6`
 * @param[out] pathname Pointer to a buffer where the constructed pathname will be stored.
 * This buffer must be at least as long as ::FNSPLIT_DOS_PATHNAME_MAX.
 * @param[in] drive Drive specifier (e.g., `C` or `C:`). If provided, it must be a valid drive name.
 * @param[in] dirname Directory path (e.g., `\path\to\a\`). If provided, it must end with a backslash.
 * @param[in] basename Base name without a suffix (e.g., `file`).
 * @param[in] suffix File suffix (e.g., `.txt`).
 * @return Bitfield that indicates which parts were added into the resulting pathname.
 * @see _wfnmerge Its LFN counterpart.
 */
int SDKLIB__afnmerge(char *pathname, const char *drive, const char *dirname, const char *basename, const char *suffix) {
    printf("--- _afnmerge called ---\n");
    printf("  [out] pathname: %p\n", (void*)pathname);
    printf("  [in] drive:    \"%s\"\n", drive ? drive : "(null)");
    printf("  [in] dirname:  \"%s\"\n", dirname ? dirname : "(null)");
    printf("  [in] basename: \"%s\"\n", basename ? basename : "(null)");
    printf("  [in] suffix:   \"%s\"\n", suffix ? suffix : "(null)");

    if (!pathname) {
        return 0;
    }

    *pathname = '\0';
    int result = 0;
    char *p = pathname;
    size_t remaining = FNSPLIT_DOS_PATHNAME_MAX;
    size_t len;

    // 1. Add drive
    if (drive && *drive) {
        len = strlen(drive);
        if (len < remaining) {
            strncpy(p, drive, len);
            p += len;
            // Add colon if not present
            if (p[-1] != ':') {
                 if (1 < remaining - len) {
                    *p++ = ':';
                 }
            }
            *p = '\0';
            remaining = FNSPLIT_DOS_PATHNAME_MAX - (p - pathname);
            result |= FNSPLIT_DRIVE;
        }
    }
    
    // 2. Add directory
    if (dirname && *dirname) {
        len = strlen(dirname);
        if (len < remaining) {
            strncat(p, dirname, len);
            p += len;
            remaining -= len;
            result |= FNSPLIT_DIR;
        }
    }

    // 3. Add basename
    if (basename && *basename) {
        len = strlen(basename);
        if (len < remaining) {
            strncat(p, basename, len);
            p += len;
            remaining -= len;
            result |= FNSPLIT_FILENAME;
        }
    }

    // 4. Add suffix
    if (suffix && *suffix) {
        len = strlen(suffix);
        if (len < remaining) {
            strncat(p, suffix, len);
            p += len;
            remaining -= len;
            result |= FNSPLIT_SUFFIX;
        }
    }
    
    printf("  [debug] Constructed -> pathname: \"%s\"\n", pathname);
    printf("  [return] %d\n\n", result);

    return result;
}



#ifndef MIN
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#endif

typedef struct loader_file_descriptor_s {
    FILE *cart;
    void *parent_fd;
    size_t subfile_base;
    size_t size;
    size_t subfile_offset;
    short unk_0x14;
    short unk_0x16;
    unsigned int unk_0x18;
    unsigned int unk_0x1c;
} loader_file_descriptor_t;

loader_file_descriptor_t *SDKLIB__OpenFile(const char *pathname, const char *mode) {
    //printf("OpenFile: %s mode: %s\n", pathname, mode);

    FILE *cart = SDKLIB__afopen(pathname, mode);
    if (!cart) return NULL;

    loader_file_descriptor_t *fd = calloc(1, sizeof(loader_file_descriptor_t));
    if (!fd) {
        fclose(cart);
        errno = ENOMEM;
        return NULL;
    }

    fd->cart = cart;
    fd->parent_fd = NULL;
    fd->subfile_base = 0;
    fd->subfile_offset = 0;
    fd->unk_0x14 = 0;
    fd->unk_0x16 = 0;
    fd->unk_0x18 = 0;
    fd->unk_0x1c = 0;

    fseek(cart, 0, SEEK_END);
    fd->size = ftell(cart);
    fseek(cart, 0, SEEK_SET);

    //printf("OpenFile: %s mode: %s result:%p\n", pathname, mode, fd);
    return fd;
}

size_t SDKLIB__FileSize(loader_file_descriptor_t *stream) {
    if (!stream) {
        errno = EINVAL;
        return (size_t)-1;
    }
    //printf("filesize: %p, result: %zu\n", (void*)stream, stream->size);
    return stream->size;
}

loader_file_descriptor_t *SDKLIB__OpenSubFile(loader_file_descriptor_t *parent, size_t base, size_t max_size) {
    if (!parent) {
        errno = EINVAL;
        return NULL;
    }
    //printf("opensubf: parent:%p\n", parent);

    if (base + max_size > parent->size) {
        fprintf(stderr, "Error: Sub-file extends beyond parent's bounds.\n");
        errno = EINVAL;
        return NULL;
    }

    loader_file_descriptor_t *fd = calloc(1, sizeof(loader_file_descriptor_t));
    if (!fd) {
        errno = ENOMEM;
        return NULL;
    }

    fd->cart = parent->cart;
    fd->parent_fd = parent;
    fd->subfile_base = parent->subfile_base + base;
    fd->size = max_size;
    fd->subfile_offset = 0;
    fd->unk_0x14 = 1;
    fd->unk_0x16 = 0;
    fd->unk_0x18 = 0;
    fd->unk_0x1c = 0;

   // printf("opensubfile: parent:%p, base:%zu, size:%zu, subf:%p\n", (void*)parent, base, max_size, (void*)fd);
    return fd;
}

int SDKLIB__CloseFile(loader_file_descriptor_t *stream) {
    if (!stream) return 0;

    //printf("closefile: %p\n", (void*)stream);
    
    if (!stream->parent_fd) {
        fclose(stream->cart);
    }
    free(stream);
    
    return 0;
}

int SDKLIB__FseekFile(loader_file_descriptor_t *stream, size_t offset, int whence) {
    if (!stream) {
        errno = EINVAL;
        return -1;
    }
    
    size_t new_offset;
    switch (whence) {
        case _SYS_SEEK_SET: new_offset = offset; break;
        case _SYS_SEEK_CUR: new_offset = stream->subfile_offset + offset; break;
        case _SYS_SEEK_END: new_offset = stream->size + offset; break;
        default: errno = EINVAL; return -1;
    }

    if (new_offset > stream->size) {
        errno = EINVAL;
        return -1;
    }
    stream->subfile_offset = new_offset;
    //printf("fseek: %p %zu %d, result:0 (new offset:%zu)\n", (void*)stream, offset, whence, new_offset);
    return 0;
}

size_t SDKLIB__ReadFile(loader_file_descriptor_t *stream, void *ptr, size_t size) {
    if (!stream || !ptr) {
        errno = EINVAL;
        return 0;
    }

    size_t bytes_remaining = stream->size - stream->subfile_offset;
    if (bytes_remaining == 0) return 0;
    
    size_t to_read = MIN(size, bytes_remaining);
    size_t absolute_offset = stream->subfile_base + stream->subfile_offset;

    if (fseek(stream->cart, absolute_offset, SEEK_SET) != 0) {
        return 0;
    }
    
    size_t bytes_read = fread(ptr, 1, to_read, stream->cart);
    stream->subfile_offset += bytes_read;

    //printf("readfile: %p %p %zu, result:%zu (abs_offset:%zu)\n", (void*)stream, ptr, size, bytes_read, absolute_offset);
    return bytes_read;
}





#pragma pack(push, 1)
typedef struct _MASTER_ID_INFO {
	char a1[40];  // 前缀信息（可能是厂商ID、地区码等）
	char a2[36];        // offset 0x58 处的序列号
	char master_id_suffix[18]; // 剩余部分（可能是校验或附加信息）
	char a3[78];        // 从属ID（MAC、IMEI 或其他）
} MASTER_ID_INFO;
#pragma pack(pop)

uint32_t SDKLIB_GetMasterIDInfo(MASTER_ID_INFO* ptr) {
printf("ID stub\n");
	memset(ptr, 0, sizeof(MASTER_ID_INFO));
	strcpy(ptr->a2, "HPPRIMEEMU");
	strcpy(ptr->a3, "BESTARTOS114514");
	return 0;
}
/**
 * @brief Basic info on a loaded applet.
 */
typedef struct loader_applet_info_s {
    /**
     * @brief Magic bytes of ROM spec.
     */
    unsigned short rom_magic;
    /**
     * @brief Unknown, seems unused.
     */
    unsigned short unused_0x2;
    /**
     * @brief Type field inside the ROM spec.
     */
    unsigned short rom_type;
    /**
     * @brief Unknown, seems unused.
     */
    unsigned short unused_0x4;
    /**
     * @brief Checksum of the executable file.
     * @details On Besta RTOS Arm, this seems to be the checksum of the PE file.
     */
    unsigned int exe_checksum;
    /**
     * @brief File size of the executable.
     */
    size_t exe_raw_size;
    /**
     * @brief Unknown, seems unused.
     */
    unsigned char unused_0x10[0x74];
} loader_applet_info_t;
int SDKLIB_GetApplicationHeadInfoA(const char* pathname,loader_applet_info_t *info){
printf("AppHead: %s\n",pathname);
return 0;
}
