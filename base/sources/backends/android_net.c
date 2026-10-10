
#include "android_native_app_glue.h"
#include <iron_net.h>
#include <iron_system.h>
#include <jni.h>
#include <stdio.h>
#include <string.h>

ANativeActivity *iron_android_get_activity(void);
jclass           iron_android_find_class(JNIEnv *env, const char *name);

void iron_net_request(const char *url_base, const char *url_path, const char *data, int port, int method, const char *headers,
                      iron_https_callback_t callback, void *callbackdata, const char *dst_path) {
	ANativeActivity *activity = iron_android_get_activity();
	JNIEnv          *env;
	JavaVM          *vm = iron_android_get_activity()->vm;
	(*vm)->AttachCurrentThread(vm, &env, NULL);
	jclass activityClass = iron_android_find_class(env, "arm.AndroidHttpRequest");

	jstring    jurl_base   = (*env)->NewStringUTF(env, url_base);
	jstring    jurl_path   = (*env)->NewStringUTF(env, url_path);
	jstring    jdata       = data ? (*env)->NewStringUTF(env, data) : NULL;
	jstring    jheaders    = headers ? (*env)->NewStringUTF(env, headers) : NULL;
	jbyteArray bytes_array = (jbyteArray)((*env)->CallStaticObjectMethod(
	    env, activityClass,
	    (*env)->GetStaticMethodID(env, activityClass, "androidHttpRequest", "(Ljava/lang/String;Ljava/lang/String;Ljava/lang/String;ILjava/lang/String;)[B"),
	    jurl_base, jurl_path, jdata, method, jheaders));
	int status = (*env)->GetStaticIntField(env, activityClass, (*env)->GetStaticFieldID(env, activityClass, "status", "I"));

	if (bytes_array == NULL) {
		callback(0, NULL, callbackdata);
		(*vm)->DetachCurrentThread(vm);
		return;
	}

	jsize  num_bytes = (*env)->GetArrayLength(env, bytes_array);
	jbyte *elements  = (*env)->GetByteArrayElements(env, bytes_array, NULL);
	if (elements != NULL) {

		if (dst_path != NULL) {
			FILE *file = fopen(dst_path, "wb");
			if (file != NULL) {
				fwrite((char *)elements, 1, num_bytes, file);
				fclose(file);
			}
			callback(status, NULL, callbackdata);
		}
		else {
			callback(status, (char *)elements, callbackdata);
		}

		// (*env)->ReleaseByteArrayElements(env, bytes_array, elements, JNI_ABORT);
	}

	(*vm)->DetachCurrentThread(vm);
}

void iron_net_update() {}

volatile uint64_t iron_net_bytes_downloaded = 0;
