#include <stdio.h>
#include <jvmti.h>
#include <string.h>
#include <stdlib.h>

#define PROG "jvm-monitor"
#define MAX_FRAME_CNT 10
#define SEPS ","

static char sel_catching_cls_sig[100];
static char sel_throwing_cls_sig[100];
static FILE *sel_log_fp;

typedef struct {
    jmethodID whm_put_method;
    jmethodID whm_contains_method;
    jobject exc_cache;
    jobject true_obj;
    jclass arrays_cls;
    jmethodID arrays_to_string_method;
} State;

static void JNICALL on_exc(jvmtiEnv *jvmti, JNIEnv *env, jthread thrd, jmethodID throwing_method, jlocation throwing_loc, jobject exc_obj, jmethodID catching_method, jlocation catching_loc) {
    jvmtiError err;
    char *exc_cls_sig = NULL;
    char *exc_cls_gen = NULL;
    const char *exc_to_string_text = NULL;
    char *catching_cls_sig = NULL;
    char *catching_cls_gen = NULL;
    char *throwing_cls_sig = NULL;
    char *throwing_cls_gen = NULL;
    jobject exc_to_string = NULL;

    jclass throwing_cls;
    err = (*jvmti)->GetMethodDeclaringClass(jvmti, throwing_method, &throwing_cls);
    if (err) {
        fprintf(stderr, PROG ": Unable to get the declaring class of throwing_method: %d\n", err);
        goto finalize_func;
    }
    err = (*jvmti)->GetClassSignature(jvmti, throwing_cls, &throwing_cls_sig, &throwing_cls_gen);
    if (err) {
        fprintf(stderr, PROG ": Unable to get the class signature of throwing_cls: %d\n", err);
        goto finalize_func;
    }
    if (strcmp(throwing_cls_sig, sel_throwing_cls_sig) != 0) {
        if (!catching_method) {
            goto finalize_func;
        }
        jclass catching_cls;
        err = (*jvmti)->GetMethodDeclaringClass(jvmti, catching_method, &catching_cls);
        if (err) {
            fprintf(stderr, PROG ": Unable to get the declaring class of catching_method: %d\n", err);
            goto finalize_func;
        }
        err = (*jvmti)->GetClassSignature(jvmti, catching_cls, &catching_cls_sig, &catching_cls_gen);
        if (err) {
            fprintf(stderr, PROG ": Unable to get the class signature of catching_cls: %d\n", err);
            goto finalize_func;
        }
        if (strcmp(catching_cls_sig, sel_catching_cls_sig) != 0) {
            //fprintf(sel_log_fp, "* Skipping an exception event: catching_cls_sig = %s, throwing_cls_sig = %s\n", catching_cls_sig, throwing_cls_sig);
            goto finalize_func;
        }
    }

    jclass exc_cls = (*env)->GetObjectClass(env, exc_obj);
    if (!exc_cls) {
        fprintf(stderr, PROG ": Unable to get the class of exc_obj\n");
        goto finalize_func;
    }

    err = (*jvmti)->GetClassSignature(jvmti, exc_cls, &exc_cls_sig, &exc_cls_gen);
    if (err) {
        fprintf(stderr, PROG ": Unable to get the class signature of exc_cls\n");
        goto finalize_func;
    }

    State *state = NULL;
    err = (*jvmti)->GetThreadLocalStorage(jvmti, thrd, (void**)&state);
    if (err) {
        fprintf(stderr, PROG ": GetThreadLocalStorage failed: %d\n", err);
        goto finalize_func;
    }
    jobject prev_exc = (*env)->CallObjectMethod(env, state->exc_cache, state->whm_put_method, exc_obj, state->true_obj);
    if (prev_exc) {
        //fprintf(stderr, PROG ": Skipping a rethrown exception: %s\n", exc_cls_sig);
        goto finalize_func;
    }

    jmethodID exc_to_string_method = (*env)->GetMethodID(env, exc_cls, "toString", "()Ljava/lang/String;");
    if (!exc_to_string_method) {
        fprintf(stderr, PROG ": Unable to get the toString() method of exc_cls\n");
        goto finalize_func;
    }
    exc_to_string = (*env)->CallObjectMethod(env, exc_obj, exc_to_string_method);
    if (!exc_to_string) {
        fprintf(stderr, PROG ": exc_obj.toString() returned null\n");
        goto finalize_func;
    }
    exc_to_string_text = (*env)->GetStringUTFChars(env, exc_to_string, NULL);
    if (!exc_to_string_text) {
        fprintf(stderr, PROG ": GetStringUTFChars(exc_to_string) returned null\n");
        goto finalize_func;
    }

    jvmtiFrameInfo frames[MAX_FRAME_CNT];
    jint frame_cnt;
    err = (*jvmti)->GetStackTrace(jvmti, thrd, 0, sizeof(frames)/sizeof(frames[0]), frames, &frame_cnt);
    if (err) {
        fprintf(stderr, PROG ": GetStackTrace() failed: %d\n", err);
        goto finalize_func;
    }

    fprintf(sel_log_fp, "---\n");
    fprintf(sel_log_fp, "exc_cls_sig=%s\n", exc_cls_sig);
    fprintf(sel_log_fp, "exc_to_string_text=%s\n", exc_to_string_text);

    for (int frame_idx=0;frame_idx<frame_cnt;frame_idx++) {
        jvmtiFrameInfo *frame = &frames[frame_idx];

        char *method_name = NULL;
        char *method_sig = NULL;
        char *method_gen = NULL;
        jvmtiLineNumberEntry *lines = NULL;
        jvmtiLocalVariableEntry *vars = NULL;
        jint var_cnt = 0;
        char *method_cls_sig = NULL;
        char *method_cls_gen = NULL;

        err = (*jvmti)->GetMethodName(jvmti, frame->method, &method_name, &method_sig, &method_gen);
        if (err) {
            fprintf(stderr, PROG ": GetMethodName() failed: %d\n", err);
            goto finalize_frame;
        }

        jint line_cnt;
        err = (*jvmti)->GetLineNumberTable(jvmti, frame->method, &line_cnt, &lines);
        if (err && err != JVMTI_ERROR_ABSENT_INFORMATION && err != JVMTI_ERROR_NATIVE_METHOD) {
            fprintf(stderr, PROG ": GetLineNumberTable() failed: %d\n", err);
            goto finalize_frame;
        }
        int line_num = -1;
        if (lines) {
            for (int i=line_cnt-1;i>=0;i--) {
                if (frame->location >= lines[i].start_location) {
                    line_num = lines[i].line_number;
                    break;
                }
            }
        }

        jclass method_cls;
        err = (*jvmti)->GetMethodDeclaringClass(jvmti, frame->method, &method_cls);
        if (err) {
            fprintf(stderr, PROG ": GetMethodDeclaringClass() failed: %d\n", err);
            goto finalize_frame;
        }

        err = (*jvmti)->GetClassSignature(jvmti, method_cls, &method_cls_sig, &method_cls_gen);
        if (err) {
            fprintf(stderr, PROG ": GetClassSignature() failed: %d\n", err);
            goto finalize_frame;
        }

        fprintf(sel_log_fp, "frame_idx=%d\n", frame_idx);
        fprintf(sel_log_fp, "method_name=%s\n", method_name);
        fprintf(sel_log_fp, "method_sig=%s\n", method_sig);
        fprintf(sel_log_fp, "line_num=%d\n", line_num);
        fprintf(sel_log_fp, "method_cls_sig=%s\n", method_cls_sig);
        fprintf(sel_log_fp, "method_cls_gen=%s\n", method_cls_gen);

        err = (*jvmti)->GetLocalVariableTable(jvmti, frame->method, &var_cnt, &vars);
        if (err && err != JVMTI_ERROR_ABSENT_INFORMATION && err != JVMTI_ERROR_NATIVE_METHOD) {
            fprintf(stderr, PROG ": GetLocalVariableTable() failed: %d\n", err);
            goto finalize_frame;
        }
        //fprintf(sel_log_fp, "* var_cnt: %d\n", var_cnt);

        int prev_slot_invalid = 0;
        int slot_idx = -1;
        int var_idx = -1;
        while (1) {
            slot_idx++;
            jobject slot_obj = NULL;
            err = (*jvmti)->GetLocalObject(jvmti, thrd, frame_idx, slot_idx, &slot_obj);
            if (err) {
                if (err == JVMTI_ERROR_OPAQUE_FRAME) {
                    break;
                } else if (err == JVMTI_ERROR_INVALID_SLOT) {
                    if (prev_slot_invalid) {
                        break;
                    }
                    prev_slot_invalid = 1;
                    continue;
                } else if (err != JVMTI_ERROR_TYPE_MISMATCH) {
                    fprintf(stderr, PROG ": GetLocalObject() failed: %d\n", err);
                    break;
                }
            }
            prev_slot_invalid = 0;
            var_idx++;
            const char *slot_name = var_idx < var_cnt ? vars[var_idx].name : "<unnamed>";

            fprintf(sel_log_fp, "local_idx=%d\n", var_idx);
            fprintf(sel_log_fp, "local_name=%s\n", slot_name);

            if (slot_obj) {
                char *slot_cls_sig = NULL;
                char *slot_cls_gen = NULL;
                const char *slot_to_string_text = NULL;
                jobject slot_to_string = NULL;

                jclass slot_cls = (*env)->GetObjectClass(env, slot_obj);
                err = (*jvmti)->GetClassSignature(jvmti, slot_cls, &slot_cls_sig, &slot_cls_gen);
                if (err) {
                    fprintf(stderr, PROG ": GetClassSignature() failed: %d\n", err);
                    goto finalize_slot_obj;
                }

                if (slot_cls_sig[0] == '[') {
                    slot_to_string = (*env)->CallStaticObjectMethod(env, state->arrays_cls, state->arrays_to_string_method, slot_obj);
                } else {
                    jmethodID slot_to_string_method = (*env)->GetMethodID(env, slot_cls, "toString", "()Ljava/lang/String;");
                    slot_to_string = (*env)->CallObjectMethod(env, slot_obj, slot_to_string_method);
                }

                slot_to_string_text = (*env)->GetStringUTFChars(env, slot_to_string, NULL);
                if (!slot_to_string_text) {
                    fprintf(stderr, PROG ": GetStringUTFChars(slot_to_string) returned null\n");
                    goto finalize_func;
                }

                fprintf(sel_log_fp, "local_type=object\n");
                fprintf(sel_log_fp, "local_cls_sig=%s\n", slot_cls_sig);
                fprintf(sel_log_fp, "local_val=%s\n", slot_to_string_text);

finalize_slot_obj:
                (*env)->ReleaseStringUTFChars(env, slot_to_string, slot_to_string_text);
                (*env)->DeleteLocalRef(env, slot_obj);
                (*jvmti)->Deallocate(jvmti, (void*)slot_cls_sig);
                (*jvmti)->Deallocate(jvmti, (void*)slot_cls_gen);
                continue;
            }

            jint slot_int;
            err = (*jvmti)->GetLocalInt(jvmti, thrd, frame_idx, slot_idx, &slot_int);
            if (!err) {
                fprintf(sel_log_fp, "local_type=int\n");
                fprintf(sel_log_fp, "local_val=%d\n", slot_int);
                continue;
            }

            jlong slot_long;
            err = (*jvmti)->GetLocalLong(jvmti, thrd, frame_idx, slot_idx, &slot_long);
            if (!err) {
                fprintf(sel_log_fp, "local_type=long\n");
                fprintf(sel_log_fp, "local_val=%ld\n", slot_long);
                continue;
            }

            jfloat slot_float;
            err = (*jvmti)->GetLocalFloat(jvmti, thrd, frame_idx, slot_idx, &slot_float);
            if (!err) {
                fprintf(sel_log_fp, "local_type=float\n");
                fprintf(sel_log_fp, "local_val=%f\n", slot_float);
                continue;
            }

            jdouble slot_double;
            err = (*jvmti)->GetLocalDouble(jvmti, thrd, frame_idx, slot_idx, &slot_double);
            if (!err) {
                fprintf(sel_log_fp, "local_type=double\n");
                fprintf(sel_log_fp, "local_val=%f\n", slot_double);
                continue;
            }

            fprintf(sel_log_fp, "local_type=invalid\n");
        }

finalize_frame:
        for (int i=0;i<var_cnt;i++) {
            (*jvmti)->Deallocate(jvmti, (void*)vars[i].name);
            (*jvmti)->Deallocate(jvmti, (void*)vars[i].signature);
            (*jvmti)->Deallocate(jvmti, (void*)vars[i].generic_signature);
        }

        (*jvmti)->Deallocate(jvmti, (void*)method_cls_sig);
        (*jvmti)->Deallocate(jvmti, (void*)method_cls_gen);
        (*jvmti)->Deallocate(jvmti, (void*)method_name);
        (*jvmti)->Deallocate(jvmti, (void*)method_sig);
        (*jvmti)->Deallocate(jvmti, (void*)method_gen);
        (*jvmti)->Deallocate(jvmti, (void*)lines);
        (*jvmti)->Deallocate(jvmti, (void*)vars);
    }

    fprintf(sel_log_fp, "---\n");

finalize_func:
    (*jvmti)->Deallocate(jvmti, (void*)exc_cls_sig);
    (*jvmti)->Deallocate(jvmti, (void*)exc_cls_gen);
    (*env)->ReleaseStringUTFChars(env, exc_to_string, exc_to_string_text);
    (*jvmti)->Deallocate(jvmti, (void*)catching_cls_sig);
    (*jvmti)->Deallocate(jvmti, (void*)catching_cls_gen);
    (*jvmti)->Deallocate(jvmti, (void*)throwing_cls_sig);
    (*jvmti)->Deallocate(jvmti, (void*)throwing_cls_gen);
}

static int apply_cfg(const char *key, const char *val) {
    if (strcmp(key, "catching_cls") == 0) {
        snprintf(sel_catching_cls_sig, sizeof(sel_catching_cls_sig), "%s", val);
    } else if (strcmp(key, "throwing_cls") == 0) {
        snprintf(sel_throwing_cls_sig, sizeof(sel_throwing_cls_sig), "%s", val);
    } else if (strcmp(key, "log_file") == 0) {
        if (sel_log_fp) {
            fclose(sel_log_fp);
        }
        sel_log_fp = fopen(val, "w");
    } else {
        fprintf(stderr, PROG ": Unrecognized configuration value: key=%s, val=%s\n", key, val);
        return 1;
    }
    return 0;
}

static int parse_opts(char *opts) {
    *sel_catching_cls_sig = '\0';
    *sel_throwing_cls_sig = '\0';
    sel_log_fp = NULL;

    for (char *token = strtok(opts, SEPS);token;token = strtok(NULL, SEPS)) {
        char *sep = strchr(token, '=');
        if (!sep) {
            fprintf(stderr, PROG ": Unrecognized configuration string: %s\n", token);
            return 1;
        }
        *sep = '\0';
        char *key = token;
        char *val = sep + 1;
        if (strcmp(key, "cfg") == 0) {
            FILE *fin = fopen(val, "r");
            if (!fin) {
                fprintf(stderr, PROG ": Unable to open the configuration file: %s\n", val);
                return 1;
            }
            char buf[512];
            while (fgets(buf, sizeof(buf), fin)) {
                char *sep = strchr(buf, '=');
                if (!sep) {
                    fclose(fin);
                    fprintf(stderr, PROG ": Unrecognized configuration string: %s\n", buf);
                    return 1;
                }
                *sep = '\0';
                char *key = buf;
                char *val = sep + 1;
                if (*val) {
                    char *val_end = val + strlen(val) - 1;
                    while (val_end >= val && (*val_end == '\n' || *val_end == '\r')) {
                        *val_end = '\0';
                        val_end--;
                    }
                }
                if (apply_cfg(key, val)) {
                    fclose(fin);
                    return 1;
                }
            }
            fclose(fin);
        } else {
            if (apply_cfg(key, val)) {
                return 1;
            }
        }
    }

    if (sel_log_fp) {
        if (setvbuf(sel_log_fp, NULL, _IONBF, 0)) {
            fprintf(stderr, PROG ": setvbuf failed\n");
            return 1;
        }
    } else {
        sel_log_fp = stderr;
    }

    return 0;
}

static void JNICALL on_thrd_start(jvmtiEnv *jvmti, JNIEnv *env, jthread thrd) {
    jvmtiError err;

    State *state = (State*)malloc(sizeof(State));
    err = (*jvmti)->SetThreadLocalStorage(jvmti, thrd, state);
    if (err) {
        fprintf(stderr, PROG ": SetThreadLocalStorage failed: %d\n", err);
        return;
    }

    jclass whm_cls = (*env)->FindClass(env, "java/util/WeakHashMap");
    jmethodID whm_init_method = (*env)->GetMethodID(env, whm_cls, "<init>", "()V");
    state->whm_put_method = (*env)->GetMethodID(env, whm_cls, "put", "(Ljava/lang/Object;Ljava/lang/Object;)Ljava/lang/Object;");
    state->whm_contains_method = (*env)->GetMethodID(env, whm_cls, "containsKey", "(Ljava/lang/Object;)Z");
    state->exc_cache = (*env)->NewObject(env, whm_cls, whm_init_method);

    jclass boolean_cls = (*env)->FindClass(env, "java/lang/Boolean");
    jmethodID boolean_value_of_method = (*env)->GetStaticMethodID(env, boolean_cls, "valueOf", "(Z)Ljava/lang/Boolean;");
    state->true_obj = (*env)->CallStaticObjectMethod(env, boolean_cls, boolean_value_of_method, (jboolean)1);

    state->arrays_cls = (*env)->FindClass(env, "java/util/Arrays");
    state->arrays_to_string_method = (*env)->GetStaticMethodID(env, state->arrays_cls, "deepToString", "([Ljava/lang/Object;)Ljava/lang/String;");
}

static void JNICALL on_thrd_end(jvmtiEnv *jvmti, JNIEnv *env, jthread thrd) {
    jvmtiError err;

    State *state = NULL;
    err = (*jvmti)->GetThreadLocalStorage(jvmti, thrd, (void**)&state);
    if (err) {
        fprintf(stderr, PROG ": GetThreadLocalStorage failed: %d\n", err);
        return;
    }
    free(state);
}

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *opts, void *reserved) {
    jvmtiError err;
    jint err2;

    if (parse_opts(opts)) {
        return 1;
    }

    jvmtiEnv *jvmti;
    err2 = (*vm)->GetEnv(vm, (void**)&jvmti, JVMTI_VERSION_1);
    if (err2) {
        fprintf(stderr, PROG ": GetEnv error: %d\n", err2);
        return 1;
    }

    jvmtiCapabilities caps = {};
    caps.can_generate_exception_events = 1;
    caps.can_get_line_numbers = 1;
    caps.can_access_local_variables = 1;
    err = (*jvmti)->AddCapabilities(jvmti, &caps);
    if (err) {
        fprintf(stderr, PROG ": AddCapabilities error: %d\n", err);
        return 1;
    }

    jvmtiEventCallbacks cbs = {};
    cbs.Exception = on_exc;
    cbs.ThreadStart = on_thrd_start;
    cbs.ThreadEnd = on_thrd_end;
    err = (*jvmti)->SetEventCallbacks(jvmti, &cbs, sizeof(cbs));
    if (err) {
        fprintf(stderr, PROG ": SetEventCallbacks error: %d\n", err);
        return 1;
    }

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_EXCEPTION, NULL);
    if (err) {
        fprintf(stderr, PROG ": SetEventNotificationMode failed for JVMTI_EVENT_EXCEPTION: %d\n", err);
        return 1;
    }

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_THREAD_START, NULL);
    if (err) {
        fprintf(stderr, PROG ": SetEventNotificationMode failed for JVMTI_EVENT_THREAD_START: %d\n", err);
        return 1;
    }

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_THREAD_END, NULL);
    if (err) {
        fprintf(stderr, PROG ": SetEventNotificationMode failed for JVMTI_EVENT_THREAD_END: %d\n", err);
        return 1;
    }

    fprintf(stderr, PROG ": Loaded successfully\n");
    return 0;
}
