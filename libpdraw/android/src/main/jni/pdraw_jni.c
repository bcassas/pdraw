/**
 * @file pdraw_jni.c
 * @brief Parrot Drones Awesome Video Viewer - Android JNI
 * @date 05/11/2016
 * @author aurelien.barre@akaaba.net
 *
 * Copyright (c) 2016 Aurelien Barre <aurelien.barre@akaaba.net>.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 * 
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 * 
 *   * Neither the name of the copyright holder nor the names of the
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <jni.h>
#include <stdlib.h>
#include <errno.h>

#include <android/log.h>
#include <android/native_window_jni.h>
#include <pdraw/pdraw.h>
#include <pthread.h>

#define LOG_TAG "pdraw_jni"

/** Log as verbose */
#define LOGV(_fmt, ...) \
    __android_log_print(ANDROID_LOG_VERBOSE, LOG_TAG, _fmt, ##__VA_ARGS__)

/** Log as debug */
#define LOGD(_fmt, ...) \
    __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, _fmt, ##__VA_ARGS__)

/** Log as info */
#define LOGI(_fmt, ...) \
    __android_log_print(ANDROID_LOG_INFO, LOG_TAG, _fmt, ##__VA_ARGS__)

/** Log as warning */
#define LOGW(_fmt, ...) \
    __android_log_print(ANDROID_LOG_WARN, LOG_TAG, _fmt, ##__VA_ARGS__)

/** Log as error */
#define LOGE(_fmt, ...) \
    __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, _fmt, ##__VA_ARGS__)
    
#define VERBOSE 1

static pthread_key_t jniEnvKey;

static struct {
   JavaVM *gJVM;
   jclass pdrawClass;
   jclass videoFrameClass;
   jclass byteBufferClass;
   jmethodID videoFrameConstructor;
   jmethodID notifyNewFrame;
} globalIds;

static void jniEnvDestructor(void* unused) {
    (*(globalIds.gJVM))->DetachCurrentThread(globalIds.gJVM);
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM *jvm, void *reserved)
{
    JNIEnv* env;
    int ret;
    
    globalIds.gJVM = jvm;
    
    if ((*(globalIds.gJVM))->GetEnv(globalIds.gJVM, (void**)&env, JNI_VERSION_1_6) != JNI_OK) {
        return -1;
    }
    
    globalIds.pdrawClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw");
    if (globalIds.pdrawClass == NULL) {
        LOGE("could not retrieve class");
        return -1;
    }
    globalIds.pdrawClass = (jclass)(*env)->NewGlobalRef(env, globalIds.pdrawClass);
    if (globalIds.pdrawClass == NULL) {
        LOGE("could not create global ref for Pdraw class");
        return -1;
    }
    globalIds.videoFrameClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$VideoFrame");
    if (globalIds.videoFrameClass == NULL) {
        return -1;
        LOGE("could not find VideoFrame class");
    }
    globalIds.videoFrameClass = (jclass)(*env)->NewGlobalRef(env, globalIds.videoFrameClass);
    if (globalIds.videoFrameClass == NULL) {
        LOGE("could not create global ref for VideoFrame class");
        return -1;
    }
    globalIds.byteBufferClass = (*env)->FindClass(env, "java/nio/ByteBuffer");
    if (globalIds.byteBufferClass == NULL) {
        return -1;
        LOGE("could not find VideoFrame class");
    }
    globalIds.byteBufferClass = (jclass)(*env)->NewGlobalRef(env, globalIds.byteBufferClass);
    if (globalIds.byteBufferClass == NULL) {
        LOGE("could not create global ref for ByteBuffer class");
        return -1;
    }
    globalIds.videoFrameConstructor = (*env)->GetMethodID(env, globalIds.videoFrameClass, "<init>", "(IIILjava/nio/ByteBuffer;IJJI)V");
    if (globalIds.videoFrameConstructor == NULL) {
        LOGE("could not find VideoFrame default constructor");
        return -1;
    }
    globalIds.notifyNewFrame = (*env)->GetMethodID(env, globalIds.pdrawClass, "notifyNewFrame", "(Lnet/akaaba/libpdraw/Pdraw$VideoFrame;)V");
    if (globalIds.notifyNewFrame == NULL) {
        LOGE("could not find notifyNewFrame method");
        return -1;
    }
    
    ret = pthread_key_create(&jniEnvKey, &jniEnvDestructor);
    if (ret != 0) {
        LOGE("pthread_key_create failed");
        return -1;
    }
    
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return -1;
    }

    return JNI_VERSION_1_6;
}

struct pdraw_jni_ctx {
    struct pdraw *pdraw;
    ANativeWindow *window;
    jobject *thizz;
    void *filterCtx;
};

static void frame_reception_callback(void *filterCtx, const pdraw_video_frame_t *frame, void *userPtr) {

   struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx *)userPtr;
    JNIEnv *env;
    jobject pixels;
    int getEnvStat;
    
    env = (JNIEnv*)pthread_getspecific(jniEnvKey);
    if (env == NULL) {
        if ((*(globalIds.gJVM))->AttachCurrentThread(globalIds.gJVM, &env, NULL) != 0) {
            LOGE("Failed to attach current thread to JVM");
            goto out;
        }
        pthread_setspecific(jniEnvKey, env);
    }

    if (frame->userDataSize == 0) {
#ifdef VERBOSE
        LOGV("frame contains no user data");
#endif
        goto out;
    }
    
    pixels = (*env)->NewDirectByteBuffer(env, frame->plane[0], (frame->stride[0] * frame->height * 3) / 2);
    if (pixels == NULL) {
        LOGE("could not allocate buffer for planes");
        goto out;
    }

    jobject videoFrame = (*env)->NewObject(env, globalIds.videoFrameClass, globalIds.videoFrameConstructor, (jint)frame->colorFormat, (jint)frame->width, (jint)frame->height,
                                              pixels, frame->stride[0], frame->auNtpTimestamp, (jlong)frame->userData, frame->userDataSize);
    if (videoFrame == NULL) {
        LOGE("could not construct a VideoFrame");
        goto free_planes;
    }

    // call Java-side callback
    (*env)->CallVoidMethod(env, ctx->thizz, globalIds.notifyNewFrame, videoFrame);

    (*env)->DeleteLocalRef(env, videoFrame);
free_planes:
    (*env)->DeleteLocalRef(env, pixels);
out:
    if ((*env)->ExceptionCheck(env)) {
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
    }
}


static void cleanup(
    JNIEnv *env,
    struct pdraw_jni_ctx *ctx)
{
    if (ctx != NULL)
    {
        if (ctx->window)
        {
            ANativeWindow_release(ctx->window);
        }
        if (ctx->pdraw != NULL)
        {
            pdraw_destroy(ctx->pdraw);
        }
        if (ctx->thizz != NULL)
        {
            (*env)->DeleteGlobalRef(env, ctx->thizz);
        }
        free(ctx);
    }
}


static jobject newScreenSettings(
    JNIEnv *env,
    jobject thizz)
{
    jclass scrClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$DisplayScreenSettings");
    jmethodID scrConstructID = (*env)->GetMethodID(env, scrClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject scr = (*env)->NewObject(env, scrClass, scrConstructID, thizz);
    return scr;
}


static void mapScreenSettingsToC(
    JNIEnv *env,
    jobject scr,
    float *c_xdpi,
    float *c_ydpi,
    float *c_deviceMargin)
{
    if ((!scr) || (!c_xdpi) || (!c_ydpi) || (!c_deviceMargin))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass scrClass = (*env)->GetObjectClass(env, scr);

    jfieldID fidXdpi = (*env)->GetFieldID(env, scrClass, "xdpi", "F");
    if (NULL == fidXdpi)
        return;
    jfieldID fidYdpi = (*env)->GetFieldID(env, scrClass, "ydpi", "F");
    if (NULL == fidYdpi)
        return;
    jfieldID fidDeviceMargin = (*env)->GetFieldID(env, scrClass, "deviceMargin", "F");
    if (NULL == fidDeviceMargin)
        return;

    jfloat xdpi = (*env)->GetFloatField(env, scr, fidXdpi);
    jfloat ydpi = (*env)->GetFloatField(env, scr, fidYdpi);
    jfloat deviceMargin = (*env)->GetFloatField(env, scr, fidDeviceMargin);

    *c_xdpi = (float)xdpi;
    *c_ydpi = (float)ydpi;
    *c_deviceMargin = (float)deviceMargin;
}


static void mapScreenSettingsFromC(
    JNIEnv *env,
    jobject scr,
    float c_xdpi,
    float c_ydpi,
    float c_deviceMargin)
{
    if (!scr)
    {
        LOGE("invalid pointer");
        return;
    }

    jclass scrClass = (*env)->GetObjectClass(env, scr);

    jfieldID fidXdpi = (*env)->GetFieldID(env, scrClass, "xdpi", "F");
    if (NULL == fidXdpi)
        return;
    jfieldID fidYdpi = (*env)->GetFieldID(env, scrClass, "ydpi", "F");
    if (NULL == fidYdpi)
        return;
    jfieldID fidDeviceMargin = (*env)->GetFieldID(env, scrClass, "deviceMargin", "F");
    if (NULL == fidDeviceMargin)
        return;

    jfloat xdpi = (jfloat)c_xdpi;
    jfloat ydpi = (jfloat)c_ydpi;
    jfloat deviceMargin = (jfloat)c_deviceMargin;

    (*env)->SetFloatField(env, scr, fidXdpi, xdpi);
    (*env)->SetFloatField(env, scr, fidYdpi, ydpi);
    (*env)->SetFloatField(env, scr, fidDeviceMargin, deviceMargin);
}


static jobject newHmdSettings(
    JNIEnv *env,
    jobject thizz)
{
    jclass hmdClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$HmdSettings");
    jmethodID hmdConstructID = (*env)->GetMethodID(env, hmdClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject hmd = (*env)->NewObject(env, hmdClass, hmdConstructID, thizz);
    return hmd;
}


static void mapHmdSettingsToC(
    JNIEnv *env,
    jobject hmd,
    pdraw_hmd_model_t *c_hmdModel,
    float *c_ipd,
    float *c_scale,
    float *c_panH,
    float *c_panV)
{
    if ((!hmd) || (!c_hmdModel) || (!c_ipd) ||
        (!c_scale) || (!c_panH) || (!c_panV))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass hmdClass = (*env)->GetObjectClass(env, hmd);

    jfieldID fidHmdModel = (*env)->GetFieldID(env, hmdClass, "hmdModel", "I");
    if (NULL == fidHmdModel)
        return;
    jfieldID fidIpd = (*env)->GetFieldID(env, hmdClass, "ipd", "F");
    if (NULL == fidIpd)
        return;
    jfieldID fidScale = (*env)->GetFieldID(env, hmdClass, "scale", "F");
    if (NULL == fidScale)
        return;
    jfieldID fidPanH = (*env)->GetFieldID(env, hmdClass, "panH", "F");
    if (NULL == fidPanH)
        return;
    jfieldID fidPanV = (*env)->GetFieldID(env, hmdClass, "panV", "F");
    if (NULL == fidPanV)
        return;

    jint hmdModel = (*env)->GetIntField(env, hmd, fidHmdModel);
    jfloat ipd = (*env)->GetFloatField(env, hmd, fidIpd);
    jfloat scale = (*env)->GetFloatField(env, hmd, fidScale);
    jfloat panH = (*env)->GetFloatField(env, hmd, fidPanH);
    jfloat panV = (*env)->GetFloatField(env, hmd, fidPanV);

    *c_hmdModel = (pdraw_hmd_model_t)hmdModel;
    *c_ipd = (float)ipd;
    *c_scale = (float)scale;
    *c_panH = (float)panH;
    *c_panV = (float)panV;
}


static void mapHmdSettingsFromC(
    JNIEnv *env,
    jobject hmd,
    pdraw_hmd_model_t c_hmdModel,
    float c_ipd,
    float c_scale,
    float c_panH,
    float c_panV)
{
    if (!hmd)
    {
        LOGE("invalid pointer");
        return;
    }

    jclass hmdClass = (*env)->GetObjectClass(env, hmd);

    jfieldID fidHmdModel = (*env)->GetFieldID(env, hmdClass, "hmdModel", "I");
    if (NULL == fidHmdModel)
        return;
    jfieldID fidIpd = (*env)->GetFieldID(env, hmdClass, "ipd", "F");
    if (NULL == fidIpd)
        return;
    jfieldID fidScale = (*env)->GetFieldID(env, hmdClass, "scale", "F");
    if (NULL == fidScale)
        return;
    jfieldID fidPanH = (*env)->GetFieldID(env, hmdClass, "panH", "F");
    if (NULL == fidPanH)
        return;
    jfieldID fidPanV = (*env)->GetFieldID(env, hmdClass, "panV", "F");
    if (NULL == fidPanV)
        return;

    jint hmdModel = (jint)c_hmdModel;
    jfloat ipd = (jfloat)c_ipd;
    jfloat scale = (jfloat)c_scale;
    jfloat panH = (jfloat)c_panH;
    jfloat panV = (jfloat)c_panV;

    (*env)->SetFloatField(env, hmd, fidHmdModel, hmdModel);
    (*env)->SetFloatField(env, hmd, fidIpd, ipd);
    (*env)->SetFloatField(env, hmd, fidScale, scale);
    (*env)->SetFloatField(env, hmd, fidPanH, panH);
    (*env)->SetFloatField(env, hmd, fidPanV, panV);
}


static jobject newLocation(
    JNIEnv *env,
    jobject thizz)
{
    jclass locClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$Location");
    jmethodID locConstructID = (*env)->GetMethodID(env, locClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject loc = (*env)->NewObject(env, locClass, locConstructID, thizz);
    return loc;
}


static void mapLocationToC(
    JNIEnv *env,
    jobject loc,
    pdraw_location_t *c_loc)
{
    if ((!loc) || (!c_loc))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass locClass = (*env)->GetObjectClass(env, loc);

    jfieldID fidValid = (*env)->GetFieldID(env, locClass, "isValid", "Z");
    if (NULL == fidValid)
        return;
    jfieldID fidLatitude = (*env)->GetFieldID(env, locClass, "latitude", "D");
    if (NULL == fidLatitude)
        return;
    jfieldID fidLongitude = (*env)->GetFieldID(env, locClass, "longitude", "D");
    if (NULL == fidLongitude)
        return;
    jfieldID fidAltitude = (*env)->GetFieldID(env, locClass, "altitude", "D");
    if (NULL == fidAltitude)
        return;
    jfieldID fidSvCount = (*env)->GetFieldID(env, locClass, "svCount", "I");
    if (NULL == fidSvCount)
        return;

    jboolean isValid = (*env)->GetBooleanField(env, loc, fidValid);
    jdouble latitude = (*env)->GetDoubleField(env, loc, fidLatitude);
    jdouble longitude = (*env)->GetDoubleField(env, loc, fidLongitude);
    jdouble altitude = (*env)->GetDoubleField(env, loc, fidAltitude);
    jint svCount = (*env)->GetIntField(env, loc, fidSvCount);

    c_loc->isValid = (isValid == JNI_TRUE) ? 1 : 0;
    c_loc->latitude = (double)latitude;
    c_loc->longitude = (double)longitude;
    c_loc->altitude = (double)altitude;
    c_loc->svCount = (uint8_t)svCount;
}


static void mapLocationFromC(
    JNIEnv *env,
    jobject loc,
    const pdraw_location_t *c_loc)
{
    if ((!loc) || (!c_loc))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass locClass = (*env)->GetObjectClass(env, loc);

    jfieldID fidValid = (*env)->GetFieldID(env, locClass, "isValid", "Z");
    if (NULL == fidValid)
        return;
    jfieldID fidLatitude = (*env)->GetFieldID(env, locClass, "latitude", "D");
    if (NULL == fidLatitude)
        return;
    jfieldID fidLongitude = (*env)->GetFieldID(env, locClass, "longitude", "D");
    if (NULL == fidLongitude)
        return;
    jfieldID fidAltitude = (*env)->GetFieldID(env, locClass, "altitude", "D");
    if (NULL == fidAltitude)
        return;
    jfieldID fidSvCount = (*env)->GetFieldID(env, locClass, "svCount", "I");
    if (NULL == fidSvCount)
        return;

    jboolean isValid = (c_loc->isValid) ? JNI_TRUE : JNI_FALSE;
    jdouble latitude = (jdouble)c_loc->latitude;
    jdouble longitude = (jdouble)c_loc->longitude;
    jdouble altitude = (jdouble)c_loc->altitude;
    jint svCount = (jint)c_loc->svCount;

    (*env)->SetBooleanField(env, loc, fidValid, isValid);
    (*env)->SetDoubleField(env, loc, fidLatitude, latitude);
    (*env)->SetDoubleField(env, loc, fidLongitude, longitude);
    (*env)->SetDoubleField(env, loc, fidAltitude, altitude);
    (*env)->SetIntField(env, loc, fidSvCount, svCount);
}


static jobject newQuaternion(
    JNIEnv *env,
    jobject thizz)
{
    jclass quatClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$Quaternion");
    jmethodID quatConstructID = (*env)->GetMethodID(env, quatClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject quat = (*env)->NewObject(env, quatClass, quatConstructID, thizz);
    return quat;
}


static void mapQuaternionToC(
    JNIEnv *env,
    jobject quat,
    pdraw_quaternion_t *c_quat)
{
    if ((!quat) || (!c_quat))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass quatClass = (*env)->GetObjectClass(env, quat);

    jfieldID fidW = (*env)->GetFieldID(env, quatClass, "w", "F");
    if (NULL == fidW)
        return;
    jfieldID fidX = (*env)->GetFieldID(env, quatClass, "x", "F");
    if (NULL == fidX)
        return;
    jfieldID fidY = (*env)->GetFieldID(env, quatClass, "y", "F");
    if (NULL == fidY)
        return;
    jfieldID fidZ = (*env)->GetFieldID(env, quatClass, "z", "F");
    if (NULL == fidZ)
        return;

    jfloat w = (*env)->GetFloatField(env, quat, fidW);
    jfloat x = (*env)->GetFloatField(env, quat, fidX);
    jfloat y = (*env)->GetFloatField(env, quat, fidY);
    jfloat z = (*env)->GetFloatField(env, quat, fidZ);

    c_quat->w = (float)w;
    c_quat->x = (float)x;
    c_quat->y = (float)y;
    c_quat->z = (float)z;
}


static void mapQuaternionFromC(
    JNIEnv *env,
    jobject quat,
    const pdraw_quaternion_t *c_quat)
{
    if ((!quat) || (!c_quat))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass quatClass = (*env)->GetObjectClass(env, quat);

    jfieldID fidW = (*env)->GetFieldID(env, quatClass, "w", "F");
    if (NULL == fidW)
        return;
    jfieldID fidX = (*env)->GetFieldID(env, quatClass, "x", "F");
    if (NULL == fidX)
        return;
    jfieldID fidY = (*env)->GetFieldID(env, quatClass, "y", "F");
    if (NULL == fidY)
        return;
    jfieldID fidZ = (*env)->GetFieldID(env, quatClass, "z", "F");
    if (NULL == fidZ)
        return;

    jfloat w = (jfloat)c_quat->w;
    jfloat x = (jfloat)c_quat->x;
    jfloat y = (jfloat)c_quat->y;
    jfloat z = (jfloat)c_quat->z;

    (*env)->SetFloatField(env, quat, fidW, w);
    (*env)->SetFloatField(env, quat, fidX, x);
    (*env)->SetFloatField(env, quat, fidY, y);
    (*env)->SetFloatField(env, quat, fidZ, z);
}


static jobject newEuler(
    JNIEnv *env,
    jobject thizz)
{
    jclass eulerClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$Euler");
    jmethodID eulerConstructID = (*env)->GetMethodID(env, eulerClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject euler = (*env)->NewObject(env, eulerClass, eulerConstructID, thizz);
    return euler;
}


static void mapEulerToC(
    JNIEnv *env,
    jobject euler,
    pdraw_euler_t *c_euler)
{
    if ((!euler) || (!c_euler))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass eulerClass = (*env)->GetObjectClass(env, euler);

    jfieldID fidPhi = (*env)->GetFieldID(env, eulerClass, "phi", "F");
    if (NULL == fidPhi)
        return;
    jfieldID fidTheta = (*env)->GetFieldID(env, eulerClass, "theta", "F");
    if (NULL == fidTheta)
        return;
    jfieldID fidPsi = (*env)->GetFieldID(env, eulerClass, "psi", "F");
    if (NULL == fidPsi)
        return;

    jfloat phi = (*env)->GetFloatField(env, euler, fidPhi);
    jfloat theta = (*env)->GetFloatField(env, euler, fidTheta);
    jfloat psi = (*env)->GetFloatField(env, euler, fidPsi);

    c_euler->phi = (float)phi;
    c_euler->theta = (float)theta;
    c_euler->psi = (float)psi;
}


static void mapEulerFromC(
    JNIEnv *env,
    jobject euler,
    const pdraw_euler_t *c_euler)
{
    if ((!euler) || (!c_euler))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass eulerClass = (*env)->GetObjectClass(env, euler);

    jfieldID fidPhi = (*env)->GetFieldID(env, eulerClass, "phi", "F");
    if (NULL == fidPhi)
        return;
    jfieldID fidTheta = (*env)->GetFieldID(env, eulerClass, "theta", "F");
    if (NULL == fidTheta)
        return;
    jfieldID fidPsi = (*env)->GetFieldID(env, eulerClass, "psi", "F");
    if (NULL == fidPsi)
        return;

    jfloat phi = (jfloat)c_euler->phi;
    jfloat theta = (jfloat)c_euler->theta;
    jfloat psi = (jfloat)c_euler->psi;

    (*env)->SetFloatField(env, euler, fidPhi, phi);
    (*env)->SetFloatField(env, euler, fidTheta, theta);
    (*env)->SetFloatField(env, euler, fidPsi, psi);
}


static jobject newCameraOrientation(
    JNIEnv *env,
    jobject thizz)
{
    jclass camClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$CameraOrientation");
    jmethodID camConstructID = (*env)->GetMethodID(env, camClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject cam = (*env)->NewObject(env, camClass, camConstructID, thizz);
    return cam;
}


static void mapCameraOrientationToC(
    JNIEnv *env,
    jobject cam,
    float *c_pan,
    float *c_tilt)
{
    if ((!cam) || (!c_pan) || (!c_tilt))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass camClass = (*env)->GetObjectClass(env, cam);

    jfieldID fidPan = (*env)->GetFieldID(env, camClass, "pan", "F");
    if (NULL == fidPan)
        return;
    jfieldID fidTilt = (*env)->GetFieldID(env, camClass, "tilt", "F");
    if (NULL == fidTilt)
        return;

    jfloat pan = (*env)->GetFloatField(env, cam, fidPan);
    jfloat tilt = (*env)->GetFloatField(env, cam, fidTilt);

    *c_pan = (float)pan;
    *c_tilt = (float)tilt;
}


static void mapCameraOrientationFromC(
    JNIEnv *env,
    jobject cam,
    float c_pan,
    float c_tilt)
{
    if (!cam)
    {
        LOGE("invalid pointer");
        return;
    }

    jclass camClass = (*env)->GetObjectClass(env, cam);

    jfieldID fidPan = (*env)->GetFieldID(env, camClass, "pan", "F");
    if (NULL == fidPan)
        return;
    jfieldID fidTilt = (*env)->GetFieldID(env, camClass, "tilt", "F");
    if (NULL == fidTilt)
        return;

    jfloat pan = (jfloat)c_pan;
    jfloat tilt = (jfloat)c_tilt;

    (*env)->SetFloatField(env, cam, fidPan, pan);
    (*env)->SetFloatField(env, cam, fidTilt, tilt);
}


static jobject newSpeed(
    JNIEnv *env,
    jobject thizz)
{
    jclass speedClass = (*env)->FindClass(env, "net/akaaba/libpdraw/Pdraw$Speed");
    jmethodID speedConstructID = (*env)->GetMethodID(env, speedClass, "<init>", "(Lnet/akaaba/libpdraw/Pdraw;)V");
    jobject speed = (*env)->NewObject(env, speedClass, speedConstructID, thizz);
    return speed;
}


static void mapSpeedToC(
    JNIEnv *env,
    jobject speed,
    pdraw_speed_t *c_speed)
{
    if ((!speed) || (!c_speed))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass speedClass = (*env)->GetObjectClass(env, speed);

    jfieldID fidPhi = (*env)->GetFieldID(env, speedClass, "north", "F");
    if (NULL == fidPhi)
        return;
    jfieldID fidTheta = (*env)->GetFieldID(env, speedClass, "east", "F");
    if (NULL == fidTheta)
        return;
    jfieldID fidPsi = (*env)->GetFieldID(env, speedClass, "down", "F");
    if (NULL == fidPsi)
        return;

    jfloat north = (*env)->GetFloatField(env, speed, fidPhi);
    jfloat east = (*env)->GetFloatField(env, speed, fidTheta);
    jfloat down = (*env)->GetFloatField(env, speed, fidPsi);

    c_speed->north = (float)north;
    c_speed->east = (float)east;
    c_speed->down = (float)down;
}


static void mapSpeedFromC(
    JNIEnv *env,
    jobject speed,
    const pdraw_speed_t *c_speed)
{
    if ((!speed) || (!c_speed))
    {
        LOGE("invalid pointer");
        return;
    }

    jclass speedClass = (*env)->GetObjectClass(env, speed);

    jfieldID fidPhi = (*env)->GetFieldID(env, speedClass, "north", "F");
    if (NULL == fidPhi)
        return;
    jfieldID fidTheta = (*env)->GetFieldID(env, speedClass, "east", "F");
    if (NULL == fidTheta)
        return;
    jfieldID fidPsi = (*env)->GetFieldID(env, speedClass, "down", "F");
    if (NULL == fidPsi)
        return;

    jfloat north = (jfloat)c_speed->north;
    jfloat east = (jfloat)c_speed->east;
    jfloat down = (jfloat)c_speed->down;

    (*env)->SetFloatField(env, speed, fidPhi, north);
    (*env)->SetFloatField(env, speed, fidTheta, east);
    (*env)->SetFloatField(env, speed, fidPsi, down);
}


JNIEXPORT jlong JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeNew(
    JNIEnv* env,
    jobject thizz)
{
    struct pdraw_jni_ctx *ctx = calloc(1, sizeof(*ctx));

    if (ctx == NULL)
    {
        LOGE("allocation failed on context");
        goto fail;
    }

    ctx->thizz = (*env)->NewGlobalRef(env, thizz);
    if (ctx->thizz == NULL)
    {
        LOGE("failed to create object global ref");
        goto fail;
    }

    ctx->pdraw = pdraw_new();
    if (ctx->pdraw == NULL)
    {
        LOGE("pdraw_new() failed");
        goto fail;
    }

    return (jlong)(intptr_t)ctx;

fail:
    cleanup(env, ctx);
    return (jlong)(intptr_t)NULL;
}


JNIEXPORT void JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeDispose(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    cleanup(env, ctx);
}

JNIEXPORT void JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeRegisterListener(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    ctx->filterCtx = pdraw_add_video_frame_filter_callback(ctx->pdraw, 0, &frame_reception_callback, ctx);
    
    if (ctx->filterCtx == NULL)
    {
        LOGE("pdraw_add_video_frame_filter_callback() failed");
    }
}


JNIEXPORT void JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeUnregisterListener(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;
    int ret;
    
    if (ctx->filterCtx == NULL) {
       LOGE("no filter to remove");
       return;
    }
    ret = (int)pdraw_remove_video_frame_filter_callback(ctx->pdraw, 0, ctx->filterCtx);
    
    if (ret != 0)
    {
        LOGE("pdraw_remove_video_frame_filter_callback() failed (%d)", ret);
    }
}

JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeOpenUrl(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring url)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_url = (*env)->GetStringUTFChars(env, url, NULL);

    ret = pdraw_open_url(ctx->pdraw, c_url);
    if (ret != 0)
    {
        LOGE("could not open file");
    }

    (*env)->ReleaseStringUTFChars(env, url, c_url);
    
    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeOpenUrlMcast(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring url,
    jstring ifaceAddr)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_url = (*env)->GetStringUTFChars(env, url, NULL);
    const char *c_ifaceAddr = (*env)->GetStringUTFChars(env, ifaceAddr, NULL);

    ret = pdraw_open_url_mcast(ctx->pdraw, c_url, c_ifaceAddr);

    (*env)->ReleaseStringUTFChars(env, url, c_url);
    (*env)->ReleaseStringUTFChars(env, ifaceAddr, c_ifaceAddr);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeOpenSingleStream(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring srcAddr,
    jstring ifaceAddr,
    jint srcStreamPort,
    jint srcControlPort,
    jint dstStreamPort,
    jint dstControlPort,
    jint qosMode)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_srcAddr = (*env)->GetStringUTFChars(env, srcAddr, NULL);
    const char *c_ifaceAddr = (*env)->GetStringUTFChars(env, ifaceAddr, NULL);

    ret = pdraw_open_single_stream(ctx->pdraw, c_srcAddr, c_ifaceAddr,
        (int)srcStreamPort, (int)srcControlPort,
        (int)dstStreamPort, (int)dstControlPort, (int)qosMode);

    (*env)->ReleaseStringUTFChars(env, srcAddr, c_srcAddr);
    (*env)->ReleaseStringUTFChars(env, ifaceAddr, c_ifaceAddr);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeOpenMux(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong mux)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    ret = pdraw_open_mux(ctx->pdraw, (void*)(intptr_t)mux);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeOpenSdp(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring sdp,
    jstring ifaceAddr)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_sdp = (*env)->GetStringUTFChars(env, sdp, NULL);
    const char *c_ifaceAddr = (*env)->GetStringUTFChars(env, ifaceAddr, NULL);

    ret = pdraw_open_sdp(ctx->pdraw, c_sdp, c_ifaceAddr);

    (*env)->ReleaseStringUTFChars(env, sdp, c_sdp);
    (*env)->ReleaseStringUTFChars(env, ifaceAddr, c_ifaceAddr);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStart(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_start(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativePause(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_pause(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeIsPaused(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_is_paused(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStop(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_stop(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSeekTo(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong timestamp)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_seek_to(ctx->pdraw, (uint64_t)timestamp);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSeekForward(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong delta)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_seek_forward(ctx->pdraw, (uint64_t)delta);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSeekBack(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong delta)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_seek_back(ctx->pdraw, (uint64_t)delta);
}


JNIEXPORT jlong JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetDuration(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jlong)-1;
    }

    return (jlong)pdraw_get_duration(ctx->pdraw);
}


JNIEXPORT jlong JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetCurrentTime(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jlong)-1;
    }

    return (jlong)pdraw_get_current_time(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStartRecorder(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring fileName)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_fileName = (*env)->GetStringUTFChars(env, fileName, NULL);

    ret = pdraw_start_recorder(ctx->pdraw, c_fileName);

    (*env)->ReleaseStringUTFChars(env, fileName, c_fileName);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStopRecorder(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_stop_recorder(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStartResender(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring dstAddr,
    jstring ifaceAddr,
    jint srcStreamPort,
    jint srcControlPort,
    jint dstStreamPort,
    jint dstControlPort)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_dstAddr = (*env)->GetStringUTFChars(env, dstAddr, NULL);
    const char *c_ifaceAddr = (*env)->GetStringUTFChars(env, ifaceAddr, NULL);

    ret = pdraw_start_resender(ctx->pdraw, c_dstAddr, c_ifaceAddr,
        (int)srcStreamPort, (int)srcControlPort,
        (int)dstStreamPort, (int)dstControlPort);

    (*env)->ReleaseStringUTFChars(env, dstAddr, c_dstAddr);
    (*env)->ReleaseStringUTFChars(env, ifaceAddr, c_ifaceAddr);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStopResender(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_stop_resender(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStartRenderer(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jint renderX,
    jint renderY,
    jint renderWidth,
    jint renderHeight,
    jboolean hmdDistorsionCorrection,
    jboolean headtracking,
    jobject surface)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    if (ctx->window)
    {
        ret = pdraw_start_renderer(ctx->pdraw,
            0, 0, 0, 0,
            (int)renderWidth, (int)renderHeight,
            (hmdDistorsionCorrection == JNI_TRUE) ? 1 : 0,
            (headtracking == JNI_TRUE) ? 1 : 0, NULL);
        if (ret != 0)
        {
            LOGE("pdraw_start_renderer() failed on free (%d)", ret);
        }

        ANativeWindow_release(ctx->window);
        ctx->window = NULL;
    }
    if (surface)
    {
        ctx->window = ANativeWindow_fromSurface(env, surface);
        if (ctx->window == NULL)
        {
            LOGE("failed to get window from surface");
            return (jint)-1;
        }

        ret = pdraw_start_renderer(ctx->pdraw,
            0, 0, (int)renderX, (int)renderY,
            (int)renderWidth, (int)renderHeight,
            (hmdDistorsionCorrection == JNI_TRUE) ? 1 : 0,
            (headtracking == JNI_TRUE) ? 1 : 0, (void*)ctx->window);
    }

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeStopRenderer(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    ret = pdraw_stop_renderer(ctx->pdraw);

    if (ctx->window)
    {
        ANativeWindow_release(ctx->window);
        ctx->window = NULL;
    }

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeRender(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong lastRenderTime)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_render(ctx->pdraw, (uint64_t)lastRenderTime);
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfFriendlyName(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_friendlyName = pdraw_get_self_friendly_name(ctx->pdraw);

    jstring friendlyName = (*env)->NewStringUTF(env, c_friendlyName);

    return friendlyName;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfFriendlyName(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring friendlyName)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_friendlyName = (*env)->GetStringUTFChars(env, friendlyName, NULL);

    ret = pdraw_set_self_friendly_name(ctx->pdraw, c_friendlyName);

    (*env)->ReleaseStringUTFChars(env, friendlyName, c_friendlyName);

    return (jint)ret;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfSerialNumber(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_serialNumber = pdraw_get_self_serial_number(ctx->pdraw);

    jstring serialNumber = (*env)->NewStringUTF(env, c_serialNumber);

    return serialNumber;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfSerialNumber(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring serialNumber)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_serialNumber = (*env)->GetStringUTFChars(env, serialNumber, NULL);

    ret = pdraw_set_self_serial_number(ctx->pdraw, c_serialNumber);

    (*env)->ReleaseStringUTFChars(env, serialNumber, c_serialNumber);

    return (jint)ret;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfSoftwareVersion(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_softwareVersion = pdraw_get_self_software_version(ctx->pdraw);

    jstring softwareVersion = (*env)->NewStringUTF(env, c_softwareVersion);

    return softwareVersion;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfSoftwareVersion(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jstring softwareVersion)
{
    int ret = 0;
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    const char *c_softwareVersion = (*env)->GetStringUTFChars(env, softwareVersion, NULL);

    ret = pdraw_set_self_software_version(ctx->pdraw, c_softwareVersion);

    (*env)->ReleaseStringUTFChars(env, softwareVersion, c_softwareVersion);

    return (jint)ret;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeIsSelfPilot(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_is_self_pilot(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfPilot(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jboolean isPilot)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_set_self_pilot(ctx->pdraw,
        (isPilot == JNI_TRUE) ? 1 : 0);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfLocation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_location_t c_loc;
    int ret = pdraw_get_self_location(ctx->pdraw, &c_loc);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_location() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject loc = newLocation(env, thizz);
    if (loc == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapLocationFromC(env, loc, &c_loc);

    return loc;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfLocation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject loc)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_location_t c_loc;
    mapLocationToC(env, loc, &c_loc);

    return pdraw_set_self_location(ctx->pdraw, &c_loc);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfControllerBatteryLevel(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_get_self_controller_battery_level(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfControllerBatteryLevel(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jint batteryLevel)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return pdraw_set_self_controller_battery_level(ctx->pdraw, (int)batteryLevel);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfControllerOrientationQuat(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_quaternion_t c_quat;
    int ret = pdraw_get_self_controller_orientation_quat(ctx->pdraw, &c_quat);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_controller_orientation_quat() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject quat = newQuaternion(env, thizz);
    if (quat == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapQuaternionFromC(env, quat, &c_quat);

    return quat;
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfControllerOrientationEuler(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_euler_t c_euler;
    int ret = pdraw_get_self_controller_orientation_euler(ctx->pdraw, &c_euler);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_controller_orientation_euler() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject euler = newEuler(env, thizz);
    if (euler == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapEulerFromC(env, euler, &c_euler);

    return euler;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfControllerOrientationQuat(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject quat)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_quaternion_t c_quat;
    mapQuaternionToC(env, quat, &c_quat);

    return pdraw_set_self_controller_orientation_quat(ctx->pdraw, &c_quat);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfControllerOrientationEuler(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject euler)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_euler_t c_euler;
    mapEulerToC(env, euler, &c_euler);

    return pdraw_set_self_controller_orientation_euler(ctx->pdraw, &c_euler);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfHeadOrientationQuat(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_quaternion_t c_quat;
    int ret = pdraw_get_self_head_orientation_quat(ctx->pdraw, &c_quat);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_head_orientation_quat() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject quat = newQuaternion(env, thizz);
    if (quat == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapQuaternionFromC(env, quat, &c_quat);

    return quat;
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfHeadOrientationEuler(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_euler_t c_euler;
    int ret = pdraw_get_self_head_orientation_euler(ctx->pdraw, &c_euler);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_head_orientation_euler() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject euler = newEuler(env, thizz);
    if (euler == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapEulerFromC(env, euler, &c_euler);

    return euler;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfHeadOrientationQuat(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject quat)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_quaternion_t c_quat;
    mapQuaternionToC(env, quat, &c_quat);

    return pdraw_set_self_head_orientation_quat(ctx->pdraw, &c_quat);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfHeadOrientationEuler(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject euler)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_euler_t c_euler;
    mapEulerToC(env, euler, &c_euler);

    return pdraw_set_self_head_orientation_euler(ctx->pdraw, &c_euler);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfHeadRefOrientationQuat(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_quaternion_t c_quat;
    int ret = pdraw_get_self_head_ref_orientation_quat(ctx->pdraw, &c_quat);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_head_ref_orientation_quat() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject quat = newQuaternion(env, thizz);
    if (quat == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapQuaternionFromC(env, quat, &c_quat);

    return quat;
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetSelfHeadRefOrientationEuler(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_euler_t c_euler;
    int ret = pdraw_get_self_head_ref_orientation_euler(ctx->pdraw, &c_euler);
    if (ret != 0)
    {
        LOGE("pdraw_get_self_head_ref_orientation_euler() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject euler = newEuler(env, thizz);
    if (euler == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapEulerFromC(env, euler, &c_euler);

    return euler;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfHeadRefOrientationQuat(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject quat)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_quaternion_t c_quat;
    mapQuaternionToC(env, quat, &c_quat);

    return pdraw_set_self_head_ref_orientation_quat(ctx->pdraw, &c_quat);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetSelfHeadRefOrientationEuler(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject euler)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_euler_t c_euler;
    mapEulerToC(env, euler, &c_euler);

    return pdraw_set_self_head_ref_orientation_euler(ctx->pdraw, &c_euler);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeResetSelfHeadRefOrientation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return pdraw_reset_self_head_ref_orientation(ctx->pdraw);
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerFriendlyName(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_friendlyName = pdraw_get_peer_friendly_name(ctx->pdraw);

    jstring friendlyName = (*env)->NewStringUTF(env, c_friendlyName);

    return friendlyName;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerMaker(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_maker = pdraw_get_peer_maker(ctx->pdraw);

    jstring maker = (*env)->NewStringUTF(env, c_maker);

    return maker;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerModel(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_model = pdraw_get_peer_model(ctx->pdraw);

    jstring model = (*env)->NewStringUTF(env, c_model);

    return model;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerModelId(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_modelId = pdraw_get_peer_model_id(ctx->pdraw);

    jstring modelId = (*env)->NewStringUTF(env, c_modelId);

    return modelId;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerSerialNumber(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_serialNumber = pdraw_get_peer_serial_number(ctx->pdraw);

    jstring serialNumber = (*env)->NewStringUTF(env, c_serialNumber);

    return serialNumber;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerSoftwareVersion(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_softwareVersion = pdraw_get_peer_software_version(ctx->pdraw);

    jstring softwareVersion = (*env)->NewStringUTF(env, c_softwareVersion);

    return softwareVersion;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerBuildId(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_buildId = pdraw_get_peer_build_id(ctx->pdraw);

    jstring buildId = (*env)->NewStringUTF(env, c_buildId);

    return buildId;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerTitle(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_title = pdraw_get_peer_title(ctx->pdraw);

    jstring title = (*env)->NewStringUTF(env, c_title);

    return title;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerComment(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_comment = pdraw_get_peer_comment(ctx->pdraw);

    jstring comment = (*env)->NewStringUTF(env, c_comment);

    return comment;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerCopyright(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_copyright = pdraw_get_peer_copyright(ctx->pdraw);

    jstring copyright = (*env)->NewStringUTF(env, c_copyright);

    return copyright;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerRunDate(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_runDate = pdraw_get_peer_run_date(ctx->pdraw);

    jstring runDate = (*env)->NewStringUTF(env, c_runDate);

    return runDate;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerRunUuid(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_runUuid = pdraw_get_peer_run_uuid(ctx->pdraw);

    jstring runUuid = (*env)->NewStringUTF(env, c_runUuid);

    return runUuid;
}


JNIEXPORT jstring JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerMediaDate(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jstring)NULL;
    }

    const char *c_mediaDate = pdraw_get_peer_media_date(ctx->pdraw);

    jstring mediaDate = (*env)->NewStringUTF(env, c_mediaDate);

    return mediaDate;
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerTakeoffLocation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_location_t c_loc;
    int ret = pdraw_get_peer_takeoff_location(ctx->pdraw, &c_loc);
    if (ret != 0)
    {
        LOGE("pdraw_get_peer_takeoff_location() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject loc = newLocation(env, thizz);
    if (loc == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapLocationFromC(env, loc, &c_loc);

    return loc;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetPeerTakeoffLocation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject loc)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_location_t c_loc;
    mapLocationToC(env, loc, &c_loc);

    return pdraw_set_peer_takeoff_location(ctx->pdraw, &c_loc);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerHomeLocation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    pdraw_location_t c_loc;
    int ret = pdraw_get_peer_home_location(ctx->pdraw, &c_loc);
    if (ret != 0)
    {
        LOGE("pdraw_get_peer_home_location() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject loc = newLocation(env, thizz);
    if (loc == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapLocationFromC(env, loc, &c_loc);

    return loc;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetPeerHomeLocation(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject loc)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    pdraw_location_t c_loc;
    mapLocationToC(env, loc, &c_loc);

    return pdraw_set_peer_home_location(ctx->pdraw, &c_loc);
}


JNIEXPORT jlong JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetPeerRecordingDuration(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jlong)-1;
    }

    return (jlong)pdraw_get_peer_recording_duration(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetPeerRecordingDuration(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong duration)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return pdraw_set_peer_recording_duration(ctx->pdraw, (uint64_t)duration);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetCameraOrientationForHeadtracking(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    float c_pan, c_tilt;
    int ret = pdraw_get_camera_orientation_for_headtracking(ctx->pdraw, &c_pan, &c_tilt);
    if (ret != 0)
    {
        LOGE("pdraw_get_camera_orientation_for_headtracking() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject cam = newCameraOrientation(env, thizz);
    if (cam == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapCameraOrientationFromC(env, cam, c_pan, c_tilt);

    return cam;
}


#if 0
JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetMediaCount(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    //TODO
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetMediaInfo(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    juint index,
    pdraw_media_info_t *info)
{
    //TODO
}


JNIEXPORT jlong JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeAddVideoFrameFilterCallback(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    juint mediaId,
    pdraw_video_frame_filter_callback_t cb,
    void *userPtr)
{
    //TODO
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeRemoveVideoFrameFilterCallback(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    juint mediaId,
    jlong filterCtx)
{
    //TODO
}


JNIEXPORT jlong JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeAddVideoFrameProducer(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    juint mediaId)
{
    //TODO
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeRemoveVideoFrameProducer(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong producerCtx)
{
    //TODO
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetProducerLastFrame(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jlong producerCtx,
    pdraw_video_frame_t *frame,
    jlong waitUs)
{
    //TODO
}
#endif


JNIEXPORT jfloat JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetControllerRadarAngleSetting(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jfloat)-1;
    }

    return (jfloat)pdraw_get_controller_radar_angle_setting(ctx->pdraw);
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetControllerRadarAngleSetting(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jfloat angle)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    return (jint)pdraw_set_controller_radar_angle_setting(ctx->pdraw, (float)angle);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetDisplayScreenSettings(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    float xdpi = 0., ydpi = 0., deviceMargin = 0.;
    int ret = pdraw_get_display_screen_settings(
        ctx->pdraw, &xdpi, &ydpi, &deviceMargin);
    if (ret != 0)
    {
        LOGE("pdraw_get_display_screen_settings() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject scr = newScreenSettings(env, thizz);
    if (scr == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapScreenSettingsFromC(env, scr, xdpi, ydpi, deviceMargin);

    return scr;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetDisplayScreenSettings(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject scr)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    float xdpi = 0., ydpi = 0., deviceMargin = 0.;
    mapScreenSettingsToC(env, scr, &xdpi, &ydpi, &deviceMargin);

    return pdraw_set_display_screen_settings(
        ctx->pdraw, xdpi, ydpi, deviceMargin);
}


JNIEXPORT jobject JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeGetHmdDistorsionCorrectionSettings(
    JNIEnv *env,
    jobject thizz,
    jlong jctx)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jobject)NULL;
    }

    float ipd = 0., scale = 0., panH = 0., panV = 0.;
    pdraw_hmd_model_t hmdModel = PDRAW_HMD_MODEL_UNKNOWN;
    int ret = pdraw_get_hmd_distorsion_correction_settings(
        ctx->pdraw, &hmdModel, &ipd, &scale, &panH, &panV);
    if (ret != 0)
    {
        LOGE("pdraw_get_hmd_distorsion_correction_settings() failed (%d)", ret);
        return (jobject)NULL;
    }

    jobject hmd = newHmdSettings(env, thizz);
    if (hmd == NULL)
    {
        LOGE("object creation failed");
        return (jobject)NULL;
    }

    mapHmdSettingsFromC(env, hmd, hmdModel, ipd, scale, panH, panV);

    return hmd;
}


JNIEXPORT jint JNICALL
Java_net_akaaba_libpdraw_Pdraw_nativeSetHmdDistorsionCorrectionSettings(
    JNIEnv *env,
    jobject thizz,
    jlong jctx,
    jobject hmd)
{
    struct pdraw_jni_ctx *ctx = (struct pdraw_jni_ctx*)(intptr_t)jctx;

    if ((!ctx) || (!ctx->pdraw))
    {
        LOGE("invalid pointer");
        return (jint)-1;
    }

    float ipd = 0., scale = 0., panH = 0., panV = 0.;
    pdraw_hmd_model_t hmdModel = 0;
    mapHmdSettingsToC(env, hmd, &hmdModel, &ipd, &scale, &panH, &panV);

    return pdraw_set_hmd_distorsion_correction_settings(
        ctx->pdraw, hmdModel, ipd, scale, panH, panV);
}
