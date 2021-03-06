/**
 * @file pdraw_renderer_gles2.cpp
 * @brief Parrot Drones Awesome Video Viewer Library - OpenGL ES 2.0 renderer
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

#include "pdraw_renderer_gles2.hpp"
#include "pdraw_settings.hpp"
#include "pdraw_session.hpp"

#ifdef USE_GLES2

#include <string.h>
#include <unistd.h>
#include <time.h>

#define ULOG_TAG libpdraw
#include <ulog.h>


namespace Pdraw
{


Gles2Renderer::Gles2Renderer(Session *session, bool initGles2)
{
    int ret = 0;
    mRunning = false;
    mSession = session;
    mMedia = NULL;
    mWindowWidth = 0;
    mWindowHeight = 0;
    mRenderX = 0;
    mRenderY = 0;
    mRenderWidth = 0;
    mRenderHeight = 0;
    mDecoder = NULL;
    mDecoderOutputBufferQueue = NULL;
    mCurrentBuffer = NULL;
    mGles2Video = NULL;
    mGles2Hud = NULL;
    mGles2HmdFirstTexUnit = 0;
    mGles2VideoFirstTexUnit = mGles2HmdFirstTexUnit + Gles2Hmd::getTexUnitCount();
    mGles2HudFirstTexUnit = mGles2VideoFirstTexUnit + Gles2Video::getTexUnitCount();
    mHmdDistorsionCorrection = false;
    mGles2Hmd = NULL;
    mFbo = 0;
    mFboTexture = 0;
    mFboRenderBuffer = 0;

    if (ret == 0)
    {
        ret = pthread_mutex_init(&mMutex, NULL);
        if (ret != 0)
        {
            ULOGE("Gles2Renderer: mutex creation failed (%d)", ret);
            ret = -1;
        }
    }
}


Gles2Renderer::~Gles2Renderer()
{
    if (mDecoder)
    {
        int ret = removeAvcDecoder(mDecoder);
        if (ret != 0)
            ULOGE("Gles2Renderer: removeAvcDecoder() failed (%d)", ret);
    }

    destroyGles2();

    pthread_mutex_destroy(&mMutex);
}


int Gles2Renderer::initGles2()
{
    int ret = 0;

    if (ret == 0)
    {
        mGles2Video = new Gles2Video(mSession, (VideoMedia*)mMedia, mGles2VideoFirstTexUnit);
        if (!mGles2Video)
        {
            ULOGE("Gles2Renderer: failed to create Gles2Video context");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        mGles2Hud = new Gles2Hud(mSession, (VideoMedia*)mMedia, mGles2HudFirstTexUnit);
        if (!mGles2Hud)
        {
            ULOGE("Gles2Renderer: failed to create Gles2Hud context");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        // Set background color and clear buffers
        glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glDisable(GL_DITHER);
        glDisable(GL_DEPTH_TEST);
        glEnable(GL_TEXTURE_2D);

        glViewport(mRenderX, mRenderY, mRenderWidth, mRenderHeight);
    }

    if ((ret == 0) && (mHmdDistorsionCorrection))
    {
        glGenFramebuffers(1, &mFbo);
        if (mFbo <= 0)
        {
            ULOGE("Gles2Renderer: failed to create framebuffer");
            ret = -1;
        }
        glBindFramebuffer(GL_FRAMEBUFFER, mFbo);

        glGenTextures(1, &mFboTexture);
        if (mFboTexture <= 0)
        {
            ULOGE("Gles2Renderer: failed to create texture");
            ret = -1;
        }
        glActiveTexture(GL_TEXTURE0 + mGles2HmdFirstTexUnit);
        glBindTexture(GL_TEXTURE_2D, mFboTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mRenderWidth / 2, mRenderHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenRenderbuffers(1, &mFboRenderBuffer);
        if (mFboRenderBuffer <= 0)
        {
            ULOGE("Gles2Renderer: failed to create render buffer");
            ret = -1;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, mFboRenderBuffer);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, mRenderWidth / 2, mRenderHeight);

        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, mFboTexture, 0);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, mFboRenderBuffer);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        {
            ULOGE("Gles2Renderer: invalid framebuffer status");
            ret = -1;
        }

        glClear(GL_COLOR_BUFFER_BIT);
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindRenderbuffer(GL_RENDERBUFFER, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        if (ret == 0)
        {
            if ((mSession) && (mSession->getSettings()))
            {
                float xdpi = 0., ydpi = 0., deviceMargin = 0., ipd = 0., scale = 0., panH = 0., panV = 0.;
                pdraw_hmd_model_t hmdModel = PDRAW_HMD_MODEL_UNKNOWN;
                mSession->getSettings()->getDisplayScreenSettings(&xdpi, &ydpi, &deviceMargin);
                mSession->getSettings()->getHmdDistorsionCorrectionSettings(&hmdModel, &ipd, &scale, &panH, &panV);
                mGles2Hmd = new Gles2Hmd(mGles2HmdFirstTexUnit, mRenderWidth, mRenderHeight,
                    hmdModel, xdpi, ydpi, deviceMargin, ipd, scale, panH, panV);
            }
            else
            {
                mGles2Hmd = new Gles2Hmd(mGles2HmdFirstTexUnit, mRenderWidth, mRenderHeight);
            }
            if (!mGles2Hmd)
            {
                ULOGE("Gles2Renderer: failed to create GlesHmd context");
                ret = -1;
            }
        }

        if (ret != 0)
        {
            mHmdDistorsionCorrection = false;
        }
    }

    return ret;
}


int Gles2Renderer::destroyGles2()
{
    if (mGles2Video)
    {
        glClear(GL_COLOR_BUFFER_BIT);
        delete mGles2Video;
        mGles2Video = NULL;
    }
    if (mGles2Hud)
    {
        delete mGles2Hud;
        mGles2Hud = NULL;
    }
    if (mGles2Hmd)
    {
        delete mGles2Hmd;
        mGles2Hmd = NULL;
    }
    if (mFboRenderBuffer > 0)
    {
        glDeleteRenderbuffers(1, &mFboRenderBuffer);
        mFboRenderBuffer = 0;
    }
    if (mFboTexture > 0)
    {
        glDeleteTextures(1, &mFboTexture);
        mFboTexture = 0;
    }
    if (mFbo > 0)
    {
        glDeleteFramebuffers(1, &mFbo);
        mFbo = 0;
    }

    return 0;
}


int Gles2Renderer::addAvcDecoder(AvcDecoder *decoder)
{
    if (!decoder)
    {
        ULOGE("Gles2Renderer: invalid decoder pointer");
        return -1;
    }
    if (mDecoder)
    {
        ULOGE("Gles2Renderer: multiple decoders are not supported");
        return -1;
    }

    mDecoderOutputBufferQueue = decoder->addOutputQueue();
    if (mDecoderOutputBufferQueue == NULL)
    {
        ULOGE("Gles2Renderer: failed to add output queue to decoder");
        return -1;
    }

    mDecoder = decoder;
    mMedia = mDecoder->getMedia();
    if (mGles2Video)
        mGles2Video->setVideoMedia((VideoMedia*)mMedia);
    if (mGles2Hud)
        mGles2Hud->setVideoMedia((VideoMedia*)mMedia);

    return 0;
}


int Gles2Renderer::removeAvcDecoder(AvcDecoder *decoder)
{
    if (!decoder)
    {
        ULOGE("Gles2Renderer: invalid decoder pointer");
        return -1;
    }

    if (decoder != mDecoder)
    {
        ULOGE("Gles2Renderer: invalid decoder");
        return -1;
    }

    if (mDecoderOutputBufferQueue)
    {
        int ret = decoder->removeOutputQueue(mDecoderOutputBufferQueue);
        if (ret != 0)
        {
            ULOGE("Gles2Renderer: failed to remove output queue from decoder");
        }
    }

    mDecoder = NULL;
    mDecoderOutputBufferQueue = NULL;

    return 0;
}


int Gles2Renderer::setRendererParams_nolock
        (int windowWidth, int windowHeight,
         int renderX, int renderY,
         int renderWidth, int renderHeight,
         bool hmdDistorsionCorrection, bool headtracking,
         void *uiHandler)
{
    int ret = 0;

    if (ret == 0)
    {
        ret = destroyGles2();
    }

    mWindowWidth = windowWidth;
    mWindowHeight = windowHeight;
    mRenderX = renderX;
    mRenderY = renderY;
    mRenderWidth = (renderWidth) ? renderWidth : windowWidth;
    mRenderHeight = (renderHeight) ? renderHeight : windowHeight;
    mHmdDistorsionCorrection = hmdDistorsionCorrection;
    mHeadtracking = headtracking;

    if ((mRenderWidth == 0) || (mRenderHeight == 0))
    {
        return 0;
    }

    if (ret == 0)
    {
        ret = initGles2();
    }

    return (ret == 0) ? 1 : ret;
}


int Gles2Renderer::setRendererParams
        (int windowWidth, int windowHeight,
         int renderX, int renderY,
         int renderWidth, int renderHeight,
         bool hmdDistorsionCorrection, bool headtracking,
         void *uiHandler)
{
    pthread_mutex_lock(&mMutex);

    mRunning = false;

    int ret = setRendererParams_nolock(windowWidth, windowHeight, renderX, renderY,
        renderWidth, renderHeight, hmdDistorsionCorrection, headtracking, uiHandler);

    if (ret > 0)
        mRunning = true;

    pthread_mutex_unlock(&mMutex);

    return ret;
}


int Gles2Renderer::render_nolock(uint64_t lastRenderTime)
{
    int ret = 0;
    Buffer *buffer = NULL, *prevBuffer = NULL;
    int dequeueRet = 0;

    if ((mDecoder) && (mDecoder->isConfigured()))
    {
        while ((dequeueRet = mDecoder->dequeueOutputBuffer(mDecoderOutputBufferQueue, &buffer, false)) == 0)
        {
            if (prevBuffer)
            {
                int releaseRet = mDecoder->releaseOutputBuffer(prevBuffer);
                if (releaseRet != 0)
                {
                    ULOGE("Gles2Renderer: failed to release buffer (%d)", releaseRet);
                }
            }
            prevBuffer = buffer;
        }
    }

    if (!buffer)
    {
        if ((dequeueRet < 0) && (dequeueRet != -2))
        {
            ULOGE("Gles2Renderer: failed to get buffer from queue (%d)", dequeueRet);
        }
    }
    else
    {
        mCurrentBuffer = buffer;
    }

    if ((mCurrentBuffer) && (ret == 0))
    {
        avc_decoder_output_buffer_t *data = (avc_decoder_output_buffer_t*)mCurrentBuffer->getMetadataPtr();

        if ((data) && (mRenderWidth) && (mRenderHeight))
        {
            if ((ret == 0) && (mHmdDistorsionCorrection))
            {
                glBindFramebuffer(GL_FRAMEBUFFER, mFbo);
                glViewport(0, 0, mRenderWidth / 2, mRenderHeight);
                glClear(GL_COLOR_BUFFER_BIT);
            }

            if (ret == 0)
            {
                if (mGles2Video)
                {
                    gles2_video_color_conversion_t colorConversion;
                    switch (data->colorFormat)
                    {
                        default:
                        case AVCDECODER_COLORFORMAT_YUV420PLANAR:
                            colorConversion = GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB;
                            break;
                        case AVCDECODER_COLORFORMAT_YUV420SEMIPLANAR:
                            colorConversion = GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB;
                            break;
                    }
                    ret = mGles2Video->renderFrame(data->plane, data->stride, data->width, data->height,
                        data->sarWidth, data->sarHeight, (mHmdDistorsionCorrection) ? mRenderWidth / 2 : mRenderWidth,
                        mRenderHeight, colorConversion, &data->metadata, mHeadtracking);
                    if (ret != 0)
                    {
                        ULOGE("Gles2Renderer: failed to render frame");
                    }
                }
            }

            if (ret == 0)
            {
                if (mGles2Hud)
                {
                    ret = mGles2Hud->renderHud(data->width * data->sarWidth, data->height * data->sarHeight,
                        (mHmdDistorsionCorrection) ? mRenderWidth / 2 : mRenderWidth, mRenderHeight, &data->metadata,
                        mHmdDistorsionCorrection, mHeadtracking);
                    if (ret != 0)
                    {
                        ULOGE("Gles2Renderer: failed to render frame");
                    }
                }
            }

            if ((ret == 0) && (mHmdDistorsionCorrection))
            {
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glViewport(mRenderX, mRenderY, mRenderWidth, mRenderHeight);

                if (mGles2Hmd)
                {
                    ret = mGles2Hmd->renderHmd(mFboTexture, mRenderWidth / 2, mRenderHeight);
                    if (ret != 0)
                    {
                        ULOGE("Gles2Renderer: failed to render HMD distorsion correction");
                    }
                }
            }

            if (ret == 0)
            {
                struct timespec t1;
                clock_gettime(CLOCK_MONOTONIC, &t1);
                uint64_t renderTimestamp, renderTimestamp1;
                renderTimestamp1 = renderTimestamp = (uint64_t)t1.tv_sec * 1000000 + (uint64_t)t1.tv_nsec / 1000;

                uint64_t currentTime = mSession->getCurrentTime();
                uint64_t duration = mSession->getDuration();
                unsigned int cHrs = 0, cMin = 0, cSec = 0, cMsec = 0;
                unsigned int dHrs = 0, dMin = 0, dSec = 0, dMsec = 0;
                if ((currentTime > 0) && (currentTime != (uint64_t)-1))
                    pdraw_friendlyTimeFromUs(currentTime, &cHrs, &cMin, &cSec, &cMsec);
                if ((duration > 0) && (duration != (uint64_t)-1))
                    pdraw_friendlyTimeFromUs(duration, &dHrs, &dMin, &dSec, &dMsec);

                ULOGI("Gles2Renderer: %02d:%02d:%02d.%03d / %02d:%02d:%02d.%03d frame (decoding: %.2fms, rendering: %.2fms, est. latency: %.2fms) render@%.1ffps",
                      cHrs, cMin, cSec, cMsec, dHrs, dMin, dSec, dMsec,
                      (float)(data->decoderOutputTimestamp - data->demuxOutputTimestamp) / 1000.,
                      (float)(renderTimestamp1 - data->decoderOutputTimestamp) / 1000.,
                      (data->auNtpTimestampLocal != 0) ? (float)(renderTimestamp - data->auNtpTimestampLocal) / 1000. : 0.,
                      (renderTimestamp - lastRenderTime > 0) ? 1000000. / ((float)(renderTimestamp - lastRenderTime)) : 0.);
            }

            ret = 1;
        }
    }

    if (buffer)
    {
        int releaseRet = mDecoder->releaseOutputBuffer(buffer);
        if (releaseRet != 0)
        {
            ULOGE("Gles2Renderer: failed to release buffer (%d)", releaseRet);
            ret = releaseRet;
        }
    }

    return ret;
}


int Gles2Renderer::render(uint64_t lastRenderTime)
{
    pthread_mutex_lock(&mMutex);

    if (!mRunning)
    {
        pthread_mutex_unlock(&mMutex);
        return 0;
    }

    int ret = render_nolock(lastRenderTime);

    pthread_mutex_unlock(&mMutex);

    return ret;
}

}

#endif /* USE_GLES2 */
