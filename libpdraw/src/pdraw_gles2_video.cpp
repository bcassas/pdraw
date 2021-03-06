/**
 * @file pdraw_gles2_video.cpp
 * @brief Parrot Drones Awesome Video Viewer Library - OpenGL ES 2.0 video rendering
 * @date 23/11/2016
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

#include "pdraw_gles2_video.hpp"

#ifdef USE_GLES2

#define ULOG_TAG libpdraw
#include <ulog.h>
#include <math.h>

#include "pdraw_session.hpp"
#include "pdraw_media_video.hpp"


#define PDRAW_GLES2_VIDEO_DEFAULT_HFOV (78.)
#define PDRAW_GLES2_VIDEO_DEFAULT_VFOV (49.)


namespace Pdraw
{


static const GLchar *videoVertexShader =
    "uniform mat4 transform_matrix;\n"
    "attribute vec4 position;\n"
    "attribute vec2 texcoord;\n"
    "varying vec2 v_texcoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_Position = position * transform_matrix;\n"
    "    v_texcoord = texcoord;\n"
    "}\n";

static const GLchar *videoNoconvFragmentShader =
#if defined(GL_ES_VERSION_2_0) && defined(ANDROID)
    "precision mediump float;\n"
#endif
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D s_texture_0;\n"
    "uniform sampler2D s_texture_1;\n"
    "uniform sampler2D s_texture_2;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(s_texture_0, v_texcoord);\n"
    "}\n";

static const GLchar *video420PlanarFragmentShader =
#if defined(GL_ES_VERSION_2_0) && defined(ANDROID)
    "precision mediump float;\n"
#endif
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D s_texture_0;\n"
    "uniform sampler2D s_texture_1;\n"
    "uniform sampler2D s_texture_2;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    float y = texture2D(s_texture_0, v_texcoord).r;\n"
    "    float u = texture2D(s_texture_1, v_texcoord).r - 0.5;\n"
    "    float v = texture2D(s_texture_2, v_texcoord).r - 0.5;\n"
    "    \n"
    "    float r = y + 1.402 * v;\n"
    "    float g = y - 0.344 * u - 0.714 * v;\n"
    "    float b = y + 1.772 * u;\n"
    "    \n"
    "    gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";

static const GLchar *video420SemiplanarFragmentShader =
#if defined(GL_ES_VERSION_2_0) && defined(ANDROID)
    "precision mediump float;\n"
#endif
    "varying vec2 v_texcoord;\n"
    "uniform sampler2D s_texture_0;\n"
    "uniform sampler2D s_texture_1;\n"
    "uniform sampler2D s_texture_2;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    float y = texture2D(s_texture_0, v_texcoord).r;\n"
    "    vec4 uv = texture2D(s_texture_1, v_texcoord);\n"
    "    float u = uv.r - 0.5;\n"
    "    float v = uv.a - 0.5;\n"
    "    \n"
    "    float r = y + 1.402 * v;\n"
    "    float g = y - 0.344 * u - 0.714 * v;\n"
    "    float b = y + 1.772 * u;\n"
    "    \n"
    "    gl_FragColor = vec4(r, g, b, 1.0);\n"
    "}\n";


Gles2Video::Gles2Video(Session *session, VideoMedia *media, unsigned int firstTexUnit)
{
    GLint vertexShader, fragmentShaderNoconv, fragmentShaderYuvp, fragmentShaderYuvsp;
    GLint success = 0;
    unsigned int i;
    int ret = 0;

    mSession = session;
    mMedia = media;
    mFirstTexUnit = firstTexUnit;

    if (ret == 0)
    {
        vertexShader = glCreateShader(GL_VERTEX_SHADER);
        if ((vertexShader == 0) || (vertexShader == GL_INVALID_ENUM))
        {
            ULOGE("Gles2Video: failed to create vertex shader");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        glShaderSource(vertexShader, 1, &videoVertexShader, NULL);
        glCompileShader(vertexShader);
        GLint success = 0;
        glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetShaderInfoLog(vertexShader, 512, NULL, infoLog);
            ULOGE("Gles2Video: vertex shader compilation failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        fragmentShaderNoconv = glCreateShader(GL_FRAGMENT_SHADER);
        if ((fragmentShaderNoconv == 0) || (fragmentShaderNoconv == GL_INVALID_ENUM))
        {
            ULOGE("Gles2Video: failed to create fragment shader");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        glShaderSource(fragmentShaderNoconv, 1, &videoNoconvFragmentShader, NULL);
        glCompileShader(fragmentShaderNoconv);
        GLint success = 0;
        glGetShaderiv(fragmentShaderNoconv, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetShaderInfoLog(fragmentShaderNoconv, 512, NULL, infoLog);
            ULOGE("Gles2Video: fragment shader compilation failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        fragmentShaderYuvsp = glCreateShader(GL_FRAGMENT_SHADER);
        if ((fragmentShaderYuvsp == 0) || (fragmentShaderYuvsp == GL_INVALID_ENUM))
        {
            ULOGE("Gles2Video: failed to create fragment shader");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        glShaderSource(fragmentShaderYuvsp, 1, &video420SemiplanarFragmentShader, NULL);
        glCompileShader(fragmentShaderYuvsp);
        GLint success = 0;
        glGetShaderiv(fragmentShaderYuvsp, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetShaderInfoLog(fragmentShaderYuvsp, 512, NULL, infoLog);
            ULOGE("Gles2Video: fragment shader compilation failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        fragmentShaderYuvp = glCreateShader(GL_FRAGMENT_SHADER);
        if ((fragmentShaderYuvp == 0) || (fragmentShaderYuvp == GL_INVALID_ENUM))
        {
            ULOGE("Gles2Video: failed to create fragment shader");
            ret = -1;
        }
    }

    if (ret == 0)
    {
        glShaderSource(fragmentShaderYuvp, 1, &video420PlanarFragmentShader, NULL);
        glCompileShader(fragmentShaderYuvp);
        GLint success = 0;
        glGetShaderiv(fragmentShaderYuvp, GL_COMPILE_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetShaderInfoLog(fragmentShaderYuvp, 512, NULL, infoLog);
            ULOGE("Gles2Video: fragment shader compilation failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* Link shaders */
        mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE] = glCreateProgram();
        glAttachShader(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], vertexShader);
        glAttachShader(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], fragmentShaderNoconv);
        glLinkProgram(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE]);
        glGetProgramiv(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetProgramInfoLog(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], 512, NULL, infoLog);
            ULOGE("Gles2Video: program link failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* Link shaders */
        mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB] = glCreateProgram();
        glAttachShader(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], vertexShader);
        glAttachShader(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], fragmentShaderYuvp);
        glLinkProgram(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB]);
        glGetProgramiv(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetProgramInfoLog(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], 512, NULL, infoLog);
            ULOGE("Gles2Video: program link failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        /* Link shaders */
        mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB] = glCreateProgram();
        glAttachShader(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], vertexShader);
        glAttachShader(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], fragmentShaderYuvsp);
        glLinkProgram(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB]);
        glGetProgramiv(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], GL_LINK_STATUS, &success);
        if (!success)
        {
            GLchar infoLog[512];
            glGetProgramInfoLog(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], 512, NULL, infoLog);
            ULOGE("Gles2Video: program link failed '%s'", infoLog);
            ret = -1;
        }
    }

    if (ret == 0)
    {
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShaderNoconv);
        glDeleteShader(fragmentShaderYuvp);
        glDeleteShader(fragmentShaderYuvsp);
    }

    if (ret == 0)
    {
        mProgramTransformMatrix[GLES2_VIDEO_COLOR_CONVERSION_NONE] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], "transform_matrix");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_NONE][0] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], "s_texture_0");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_NONE][1] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], "s_texture_1");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_NONE][2] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], "s_texture_2");
        mPositionHandle[GLES2_VIDEO_COLOR_CONVERSION_NONE] =
                glGetAttribLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], "position");
        mTexcoordHandle[GLES2_VIDEO_COLOR_CONVERSION_NONE] =
                glGetAttribLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_NONE], "texcoord");

        mProgramTransformMatrix[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], "transform_matrix");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB][0] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], "s_texture_0");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB][1] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], "s_texture_1");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB][2] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], "s_texture_2");
        mPositionHandle[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB] =
                glGetAttribLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], "position");
        mTexcoordHandle[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB] =
                glGetAttribLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB], "texcoord");

        mProgramTransformMatrix[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], "transform_matrix");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB][0] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], "s_texture_0");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB][1] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], "s_texture_1");
        mUniformSamplers[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB][2] =
                glGetUniformLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], "s_texture_2");
        mPositionHandle[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB] =
                glGetAttribLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], "position");
        mTexcoordHandle[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB] =
                glGetAttribLocation(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB], "texcoord");

        glGenTextures(GLES2_VIDEO_TEX_UNIT_COUNT, mTextures);

        for (i = 0; i < GLES2_VIDEO_TEX_UNIT_COUNT; i++)
        {
            glActiveTexture(GL_TEXTURE0 + mFirstTexUnit + i);
            glBindTexture(GL_TEXTURE_2D, mTextures[i]);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        }
    }
}


Gles2Video::~Gles2Video()
{
    glDeleteTextures(GLES2_VIDEO_TEX_UNIT_COUNT, mTextures);
    glDeleteProgram(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB]);
    glDeleteProgram(mProgram[GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB]);
}


int Gles2Video::renderFrame(uint8_t *framePlane[3], unsigned int frameStride[3],
                           unsigned int frameWidth, unsigned int frameHeight,
                           unsigned int sarWidth, unsigned int sarHeight,
                           unsigned int windowWidth, unsigned int windowHeight,
                           gles2_video_color_conversion_t colorConversion,
                           const video_frame_metadata_t *metadata,
                           bool headtracking)
{
    unsigned int i;
    float vertices[8];
    float texCoords[8];
    float transformMatrix[16];

    if ((frameWidth == 0) || (frameHeight == 0) || (sarWidth == 0) || (sarHeight == 0)
            || (windowWidth == 0) || (windowHeight == 0) || (frameStride[0] == 0))
    {
        ULOGE("Gles2Video: invalid dimensions");
        return -1;
    }

    glUseProgram(mProgram[colorConversion]);
    glEnable(GL_TEXTURE_2D);
    glClear(GL_COLOR_BUFFER_BIT);

    switch (colorConversion)
    {
        default:
        case GLES2_VIDEO_COLOR_CONVERSION_NONE:
            glUniform1i(mUniformSamplers[colorConversion][0], mFirstTexUnit + (long int)framePlane[0]);
            break;
        case GLES2_VIDEO_COLOR_CONVERSION_YUV420PLANAR_TO_RGB:
            for (i = 0; i < GLES2_VIDEO_TEX_UNIT_COUNT; i++)
            {
                glActiveTexture(GL_TEXTURE0 + mFirstTexUnit + i);
                glBindTexture(GL_TEXTURE_2D, mTextures[i]);
                glUniform1i(mUniformSamplers[colorConversion][i], mFirstTexUnit + i);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frameStride[i], frameHeight / ((i > 0) ? 2 : 1),
                             0, GL_LUMINANCE, GL_UNSIGNED_BYTE, framePlane[i]);
            }
            break;
        case GLES2_VIDEO_COLOR_CONVERSION_YUV420SEMIPLANAR_TO_RGB:
            glActiveTexture(GL_TEXTURE0 + mFirstTexUnit + 0);
            glBindTexture(GL_TEXTURE_2D, mTextures[0]);
            glUniform1i(mUniformSamplers[colorConversion][0],  mFirstTexUnit + 0);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE, frameStride[0], frameHeight,
                         0, GL_LUMINANCE, GL_UNSIGNED_BYTE, framePlane[0]);

            glActiveTexture(GL_TEXTURE0 + mFirstTexUnit + 1);
            glBindTexture(GL_TEXTURE_2D, mTextures[1]);
            glUniform1i(mUniformSamplers[colorConversion][1], mFirstTexUnit + 1);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, frameStride[1] / 2, frameHeight / 2,
                         0, GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, framePlane[1]);
            break;
    }

    /* Keep the video aspect ratio */
    float windowAR = (float)windowWidth / (float)windowHeight;
    float sar = (float)sarWidth / (float)sarHeight;
    float videoAR = (float)frameWidth / (float)frameHeight * sar;
    float windowW = 1.;
    float windowH = windowAR;
    float ratioW = 1.;
    float ratioH = 1.;
    if (videoAR >= windowAR)
    {
        ratioW = 1.;
        ratioH = windowAR / videoAR;
        windowW = 1.;
        windowH = windowAR;
    }
    else
    {
        ratioW = videoAR / windowAR;
        ratioH = 1.;
        windowW = 1.;
        windowH = windowAR;
    }
    float videoW = ratioW / windowW;
    float videoH = ratioH / windowH;
    float deltaX = 0.;
    float deltaY = 0.;
    float angle = 0.;
    if ((headtracking) && (mSession))
    {
        quaternion_t headQuat, headRefQuat;
        mSession->getSelfMetadata()->getHeadOrientation(&headQuat);
        mSession->getSelfMetadata()->getHeadRefOrientation(&headRefQuat);

        /* diff * headRefQuat = headQuat  --->  diff = headQuat * inverse(headRefQuat) */
        quaternion_t headDiff, headRefQuatInv;
        pdraw_quat_conj(&headRefQuat, &headRefQuatInv);
        pdraw_quat_mult(&headQuat, &headRefQuatInv, &headDiff);
        euler_t headOrientation;
        pdraw_quat2euler(&headDiff, &headOrientation);
        float hFov = 0.;
        float vFov = 0.;
        if (mMedia)
            mMedia->getFov(&hFov, &vFov);
        if (hFov == 0.)
            hFov = PDRAW_GLES2_VIDEO_DEFAULT_HFOV;
        if (vFov == 0.)
            vFov = PDRAW_GLES2_VIDEO_DEFAULT_VFOV;
        hFov = hFov * M_PI / 180.;
        vFov = vFov * M_PI / 180.;
        float scaleW = hFov / ratioW;
        float scaleH = vFov / ratioH;
        deltaX = (headOrientation.psi - metadata->cameraPan) / scaleW * 2.;
        deltaY = (headOrientation.theta - metadata->cameraTilt) / scaleH * 2.;
        angle = headOrientation.phi;
    }

    vertices[0] = -videoW;
    vertices[1] = -videoH;
    vertices[2] = videoW;
    vertices[3] = -videoH;
    vertices[4] = -videoW;
    vertices[5] = videoH;
    vertices[6] = videoW;
    vertices[7] = videoH;

    transformMatrix[0] = cosf(angle) * windowW;
    transformMatrix[1] = -sinf(angle) * windowW;
    transformMatrix[2] = 0;
    transformMatrix[3] = -deltaX;
    transformMatrix[4] = sinf(angle) * windowH;
    transformMatrix[5] = cosf(angle) * windowH;
    transformMatrix[6] = 0;
    transformMatrix[7] = -deltaY;
    transformMatrix[8] = 0;
    transformMatrix[9] = 0;
    transformMatrix[10] = 1;
    transformMatrix[11] = 0;
    transformMatrix[12] = 0;
    transformMatrix[13] = 0;
    transformMatrix[14] = 0;
    transformMatrix[15] = 1;

    glUniformMatrix4fv(mProgramTransformMatrix[colorConversion], 1, false, transformMatrix);

    glVertexAttribPointer(mPositionHandle[colorConversion], 2, GL_FLOAT, false, 0, vertices);
    glEnableVertexAttribArray(mPositionHandle[colorConversion]);

    texCoords[0] = 0.0f;
    texCoords[1] = 1.0f;
    texCoords[2] = (float)frameWidth / (float)frameStride[0];
    texCoords[3] = 1.0f;
    texCoords[4] = 0.0f;
    texCoords[5] = 0.0f;
    texCoords[6] = (float)frameWidth / (float)frameStride[0];
    texCoords[7] = 0.0f;

    glVertexAttribPointer(mTexcoordHandle[colorConversion], 2, GL_FLOAT, false, 0, texCoords);
    glEnableVertexAttribArray(mTexcoordHandle[colorConversion]);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray(mPositionHandle[colorConversion]);
    glDisableVertexAttribArray(mTexcoordHandle[colorConversion]);

    return 0;
}


GLuint* Gles2Video::getTextures()
{
    return mTextures;
}

int Gles2Video::allocTextures(unsigned int videoWidth, unsigned int videoHeight)
{
    int ret = 0, i;

    for (i = 0; i < GLES2_VIDEO_TEX_UNIT_COUNT; i++)
    {
        glActiveTexture(GL_TEXTURE0 + mFirstTexUnit + i);
        glBindTexture(GL_TEXTURE_2D, mTextures[i]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, videoWidth, videoHeight, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, NULL);
    }

    return ret;
}


void Gles2Video::setVideoMedia(VideoMedia *media)
{
    mMedia = media;
}

}

#endif /* USE_GLES2 */
