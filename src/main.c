#include <stdio.h>
#include <jvmti.h>
#include <string.h>

#define PROG "jvm-monitor"
#define MAX_FRAME_CNT 5
#define SEPS ","

static char sel_catch_cls_sig[100];
static FILE *sel_log_fp;

static void JNICALL on_exc(jvmtiEnv *jvmti, JNIEnv *env, jthread thread, jmethodID method, jlocation loc, jobject exc_obj, jmethodID catch_method, jlocation catch_loc) {
    jvmtiError err;
    char *exc_cls_sig = NULL;
    char *exc_cls_gen = NULL;
    const char *exc_to_string_text = NULL;
    char *catch_cls_sig = NULL;
    char *catch_cls_gen = NULL;
    jobject exc_to_string_ret = NULL;

    jclass catch_cls;
    err = (*jvmti)->GetMethodDeclaringClass(jvmti, catch_method, &catch_cls);
    if (err) {
        fprintf(stderr, PROG ": Unable to get the declaring class of catch_method: %d\n", err);
        goto finalize_func;
    }
    err = (*jvmti)->GetClassSignature(jvmti, catch_cls, &catch_cls_sig, &catch_cls_gen);
    if (err) {
        fprintf(stderr, PROG ": Unable to get the class signature of catch_cls: %d\n", err);
        goto finalize_func;
    }
    if (strcmp(catch_cls_sig, sel_catch_cls_sig) != 0) {
        fprintf(sel_log_fp, "* Skipping catch_cls_sig = %s\n", catch_cls_sig);
        goto finalize_func;
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

    jmethodID exc_to_string = (*env)->GetMethodID(env, exc_cls, "toString", "()Ljava/lang/String;");
    if (!exc_to_string) {
        fprintf(stderr, PROG ": Unable to get the toString() method of exc_cls\n");
        goto finalize_func;
    }
    exc_to_string_ret = (*env)->CallObjectMethod(env, exc_obj, exc_to_string);
    if (!exc_to_string_ret) {
        fprintf(stderr, PROG ": exc_obj.toString() returned null\n");
        goto finalize_func;
    }

    exc_to_string_text = (*env)->GetStringUTFChars(env, exc_to_string_ret, NULL);
    if (!exc_to_string_text) {
        fprintf(stderr, PROG ": GetStringUTFChars() returned null\n");
        goto finalize_func;
    }

    jvmtiFrameInfo frames[MAX_FRAME_CNT];
    jint frame_cnt;
    err = (*jvmti)->GetStackTrace(jvmti, thread, 0, sizeof(frames)/sizeof(frames[0]), frames, &frame_cnt);
    if (err) {
        fprintf(stderr, PROG ": GetStackTrace() failed: %d\n", err);
        goto finalize_func;
    }
    for (int frame_idx=0;frame_idx<frame_cnt;frame_idx++) {
        jvmtiFrameInfo *frame = &frames[frame_idx];

        char *method_name = NULL;
        char *method_sig = NULL;
        char *method_gen = NULL;
        jvmtiLineNumberEntry *lines = NULL;
        jvmtiLocalVariableEntry *vars = NULL;
        jint var_cnt = 0;

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

        fprintf(sel_log_fp, "* frame[%d] = %s (line %d)\n", frame_idx, method_name, line_num);

        err = (*jvmti)->GetLocalVariableTable(jvmti, frame->method, &var_cnt, &vars);
        if (err && err != JVMTI_ERROR_ABSENT_INFORMATION && err != JVMTI_ERROR_NATIVE_METHOD) {
            fprintf(stderr, PROG ": GetLocalVariableTable() failed: %d\n", err);
            goto finalize_frame;
        }
        fprintf(sel_log_fp, "* var_cnt: %d\n", var_cnt);

        int prev_slot_invalid = 0;
        int slot_idx = -1;
        int var_idx = -1;
        while (1) {
            slot_idx++;
            jobject slot_obj = NULL;
            err = (*jvmti)->GetLocalObject(jvmti, thread, frame_idx, slot_idx, &slot_obj);
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
            if (slot_obj) {
                char *slot_cls_sig = NULL;
                char *slot_cls_gen = NULL;
                const char *slot_to_string_text = NULL;

                jclass slot_cls = (*env)->GetObjectClass(env, slot_obj);
                jmethodID slot_to_string = (*env)->GetMethodID(env, slot_cls, "toString", "()Ljava/lang/String;");
                jobject slot_to_string_ret = (*env)->CallObjectMethod(env, slot_obj, slot_to_string);
                slot_to_string_text = (*env)->GetStringUTFChars(env, slot_to_string_ret, NULL);

                err = (*jvmti)->GetClassSignature(jvmti, slot_cls, &slot_cls_sig, &slot_cls_gen);
                if (err) {
                    fprintf(stderr, PROG ": GetClassSignature() failed: %d\n", err);
                    goto finalize_slot_obj;
                }

                fprintf(sel_log_fp, "* local_vars[%d/%s] = object %s (\"%s\")\n", var_idx, slot_name, slot_cls_sig, slot_to_string_text);

finalize_slot_obj:
                (*env)->ReleaseStringUTFChars(env, slot_to_string_ret, slot_to_string_text);
                (*env)->DeleteLocalRef(env, slot_obj);
                (*jvmti)->Deallocate(jvmti, (void*)slot_cls_sig);
                (*jvmti)->Deallocate(jvmti, (void*)slot_cls_gen);
                continue;
            }

            jint slot_int;
            err = (*jvmti)->GetLocalInt(jvmti, thread, frame_idx, slot_idx, &slot_int);
            if (!err) {
                fprintf(sel_log_fp, "* local_vars[%d/%s] = int %d\n", var_idx, slot_name, slot_int);
                continue;
            }

            jlong slot_long;
            err = (*jvmti)->GetLocalLong(jvmti, thread, frame_idx, slot_idx, &slot_long);
            if (!err) {
                fprintf(sel_log_fp, "* local_vars[%d/%s] = long %ld\n", var_idx, slot_name, slot_long);
                continue;
            }

            jfloat slot_float;
            err = (*jvmti)->GetLocalFloat(jvmti, thread, frame_idx, slot_idx, &slot_float);
            if (!err) {
                fprintf(sel_log_fp, "* local_vars[%d/%s] = float %f\n", var_idx, slot_name, slot_float);
                continue;
            }

            jdouble slot_double;
            err = (*jvmti)->GetLocalDouble(jvmti, thread, frame_idx, slot_idx, &slot_double);
            if (!err) {
                fprintf(sel_log_fp, "* local_vars[%d/%s] = double %f\n", var_idx, slot_name, slot_double);
                continue;
            }

            fprintf(sel_log_fp, "* local_vars[%d/%s] = invalid type\n", var_idx, slot_name);
        }

finalize_frame:
        for (int i=0;i<var_cnt;i++) {
            (*jvmti)->Deallocate(jvmti, (void*)vars[i].name);
            (*jvmti)->Deallocate(jvmti, (void*)vars[i].signature);
            (*jvmti)->Deallocate(jvmti, (void*)vars[i].generic_signature);
        }

        (*jvmti)->Deallocate(jvmti, (void*)method_name);
        (*jvmti)->Deallocate(jvmti, (void*)method_sig);
        (*jvmti)->Deallocate(jvmti, (void*)method_gen);
        (*jvmti)->Deallocate(jvmti, (void*)lines);
        (*jvmti)->Deallocate(jvmti, (void*)vars);
    }

finalize_func:
    (*jvmti)->Deallocate(jvmti, (void*)exc_cls_sig);
    (*jvmti)->Deallocate(jvmti, (void*)exc_cls_gen);
    (*env)->ReleaseStringUTFChars(env, exc_to_string_ret, exc_to_string_text);
    (*jvmti)->Deallocate(jvmti, (void*)catch_cls_sig);
    (*jvmti)->Deallocate(jvmti, (void*)catch_cls_gen);
}

static int apply_cfg(const char *key, const char *val) {
    if (strcmp(key, "catch_cls") == 0) {
        snprintf(sel_catch_cls_sig, sizeof(sel_catch_cls_sig), "%s", val);
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
    *sel_catch_cls_sig = '\0';
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

JNIEXPORT jint JNICALL Agent_OnLoad(JavaVM *vm, char *opts, void *reserved) {
    jvmtiError err;
    jint err2;

    fprintf(stderr, PROG ": loaded\n");

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
    err = (*jvmti)->SetEventCallbacks(jvmti, &cbs, sizeof(cbs));
    if (err) {
        fprintf(stderr, PROG ": SetEventCallbacks error: %d\n", err);
        return 1;
    }

    err = (*jvmti)->SetEventNotificationMode(jvmti, JVMTI_ENABLE, JVMTI_EVENT_EXCEPTION, NULL);
    if (err) {
        fprintf(stderr, PROG ": SetEventNotificationMode error: %d\n", err);
        return 1;
    }

    return 0;
}
