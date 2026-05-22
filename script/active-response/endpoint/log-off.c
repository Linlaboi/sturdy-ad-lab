#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>
#include <wtsapi32.h>
#include <cJSON.h>

#pragma comment(lib, "wtsapi32.lib")

#define LOG_FILE "C:\\Program Files (x86)\\ossec-agent\\active-response\\bin\\logoff-debug.log"
#define MAX_INPUT 8192
#define AR_VERSION 2
#define AR_MODULE_NAME "active-response"
#define CHECK_KEYS_ENTRY "check_keys"

// Logging function (matches official pattern - open, write, close immediately)
void write_debug_file(const char* ar_name, const char* msg) {
    SYSTEMTIME st;
    GetLocalTime(&st);
    
    FILE* log = fopen(LOG_FILE, "a");
    if (log) {
        fprintf(log, "%04d-%02d-%02d %02d:%02d:%02d %s: %s\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond,
                ar_name, msg);
        fclose(log);  // Close immediately to avoid locking
    }
}

// Build deduplication message (matches official build_json_keys_message)
char* build_json_keys_message(const char* ar_name, char** keys) {
    cJSON* message = cJSON_CreateObject();
    cJSON* origin_obj = cJSON_CreateObject();
    cJSON* params_obj = cJSON_CreateObject();
    cJSON* keys_array = cJSON_CreateArray();
    
    cJSON_AddNumberToObject(message, "version", AR_VERSION);
    
    cJSON_AddItemToObject(message, "origin", origin_obj);
    cJSON_AddStringToObject(origin_obj, "name", ar_name ? ar_name : "");
    cJSON_AddStringToObject(origin_obj, "module", AR_MODULE_NAME);
    
    cJSON_AddStringToObject(message, "command", CHECK_KEYS_ENTRY);
    
    cJSON_AddItemToObject(message, "parameters", params_obj);
    cJSON_AddItemToObject(params_obj, "keys", keys_array);
    
    for (int i = 0; keys != NULL && keys[i] != NULL; i++) {
        cJSON_AddItemToArray(keys_array, cJSON_CreateString(keys[i]));
    }
    
    char* msg = cJSON_PrintUnformatted(message);
    cJSON_Delete(message);
    return msg;
}

// Extract username (matches official get_username_from_json pattern)
char* get_username_from_json(cJSON* root) {
    // Try ECS path: user.name
    cJSON* user = cJSON_GetObjectItem(root, "user");
    if (user) {
        cJSON* name = cJSON_GetObjectItem(user, "name");
        if (name && cJSON_IsString(name)) {
            return strdup(name->valuestring);
        }
    }
    
    // Fallback: parameters.extra_args[0]
    cJSON* params = cJSON_GetObjectItem(root, "parameters");
    if (params) {
        cJSON* extra = cJSON_GetObjectItem(params, "extra_args");
        if (extra && cJSON_IsArray(extra) && cJSON_GetArraySize(extra) > 0) {
            cJSON* first = cJSON_GetArrayItem(extra, 0);
            if (first && cJSON_IsString(first)) {
                return strdup(first->valuestring);
            }
        }
    }
    
    return NULL;
}

// Send keys and check (matches official send_keys_and_check_message)
int send_keys_and_check_message(const char* ar_name, char** keys) {
    char input[MAX_INPUT];
    char* keys_msg;
    
    // Build and send message
    keys_msg = build_json_keys_message(ar_name, keys);
    write_debug_file(ar_name, keys_msg);
    
    printf("%s\n", keys_msg);
    fflush(stdout);  // ⚠️ CRITICAL - Force flush before reading response
    
    free(keys_msg);
    
    // Read response from execd
    memset(input, '\0', MAX_INPUT);
    if (fgets(input, MAX_INPUT, stdin) == NULL) {
        write_debug_file(ar_name, "Cannot read response from execd");
        return -1;
    }
    
    write_debug_file(ar_name, input);
    
    cJSON* response = cJSON_Parse(input);
    if (!response) {
        write_debug_file(ar_name, "Invalid response format");
        return -1;
    }
    
    cJSON* cmd = cJSON_GetObjectItem(response, "command");
    int result = -1;
    
    if (cmd && cJSON_IsString(cmd)) {
        if (strcmp(cmd->valuestring, "continue") == 0) {
            result = 1;  // Continue
        } else if (strcmp(cmd->valuestring, "abort") == 0) {
            result = 0;  // Abort (duplicate)
        }
    }
    
    cJSON_Delete(response);
    return result;
}

// Find session ID
DWORD find_session_id(const char* username) {
    PWTS_SESSION_INFO sessions = NULL;
    DWORD count = 0;
    DWORD session_id = 0xFFFFFFFF;
    
    if (!WTSEnumerateSessions(WTS_CURRENT_SERVER_HANDLE, 0, 1, &sessions, &count)) {
        return session_id;
    }
    
    for (DWORD i = 0; i < count; i++) {
        if (sessions[i].State == WTSActive) {
            LPSTR user_name = NULL;
            DWORD bytes = 0;
            
            if (WTSQuerySessionInformation(WTS_CURRENT_SERVER_HANDLE,
                                          sessions[i].SessionId,
                                          WTSUserName,
                                          &user_name,
                                          &bytes)) {
                if (_stricmp(user_name, username) == 0) {
                    session_id = sessions[i].SessionId;
                    WTSFreeMemory(user_name);
                    break;
                }
                WTSFreeMemory(user_name);
            }
        }
    }
    
    WTSFreeMemory(sessions);
    return session_id;
}

int main(int argc, char** argv) {
    char input[MAX_INPUT];
    char log_msg[512];
    const char* ar_name = "log-off";
    
    write_debug_file(ar_name, "Starting");
    
    // STEP 1: Read initial command from stdin
    memset(input, '\0', MAX_INPUT);
    if (fgets(input, MAX_INPUT, stdin) == NULL) {
        write_debug_file(ar_name, "Cannot read input from stdin");
        return 1;
    }
    
    write_debug_file(ar_name, input);
    
    // Parse JSON
    cJSON* root = cJSON_Parse(input);
    if (!root) {
        write_debug_file(ar_name, "Failed to parse JSON");
        return 1;
    }
    
    // Extract command
    cJSON* cmd_obj = cJSON_GetObjectItem(root, "command");
    if (!cmd_obj || !cJSON_IsString(cmd_obj)) {
        write_debug_file(ar_name, "No command field");
        cJSON_Delete(root);
        return 1;
    }
    
    const char* command = cmd_obj->valuestring;
    
    // Only process "enable" (disable would re-enable the account)
    if (strcmp(command, "enable") != 0) {
        snprintf(log_msg, sizeof(log_msg), "Ignoring command: %s", command);
        write_debug_file(ar_name, log_msg);
        cJSON_Delete(root);
        return 0;
    }
    
    // Extract username
    char* username = get_username_from_json(root);
    if (!username) {
        write_debug_file(ar_name, "Failed to extract username");
        cJSON_Delete(root);
        return 1;
    }
    
    snprintf(log_msg, sizeof(log_msg), "Target user: %s", username);
    write_debug_file(ar_name, log_msg);
    
    // STEP 2: Send deduplication check and wait for response
    char* keys[2] = { username, NULL };
    int check_result = send_keys_and_check_message(ar_name, keys);
    
    if (check_result == 0) {
        write_debug_file(ar_name, "Aborted by execd: duplicate key");
        free(username);
        cJSON_Delete(root);
        return 0;
    } else if (check_result < 0) {
        write_debug_file(ar_name, "Failed deduplication check");
        free(username);
        cJSON_Delete(root);
        return 1;
    }
    
    // Skip protected accounts
    if (_stricmp(username, "SYSTEM") == 0 || _stricmp(username, "Administrator") == 0) {
        write_debug_file(ar_name, "Skipped: Protected account");
        free(username);
        cJSON_Delete(root);
        return 0;
    }
    
    // Find active session
    DWORD session_id = find_session_id(username);
    if (session_id == 0xFFFFFFFF) {
        snprintf(log_msg, sizeof(log_msg), "No active session for %s", username);
        write_debug_file(ar_name, log_msg);
        free(username);
        cJSON_Delete(root);
        return 0;
    }
    
    snprintf(log_msg, sizeof(log_msg), "Found session ID: %lu", session_id);
    write_debug_file(ar_name, log_msg);
    
    // Logoff the session
    if (!WTSLogoffSession(WTS_CURRENT_SERVER_HANDLE, session_id, FALSE)) {
        snprintf(log_msg, sizeof(log_msg), "Failed to logoff session %lu (Error: %lu)",
                 session_id, GetLastError());
        write_debug_file(ar_name, log_msg);
        free(username);
        cJSON_Delete(root);
        return 1;
    }
    
    snprintf(log_msg, sizeof(log_msg), "SUCCESS: Logged off session %lu", session_id);
    write_debug_file(ar_name, log_msg);
    
    free(username);
    cJSON_Delete(root);
    return 0;
}