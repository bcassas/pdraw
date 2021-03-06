/**
 * @file pdraw_metadata_videoframe.hpp
 * @brief Parrot Drones Awesome Video Viewer Library - video frame metadata
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

#ifndef _PDRAW_METADATA_VIDEOFRAME_HPP_
#define _PDRAW_METADATA_VIDEOFRAME_HPP_

#include <inttypes.h>
#include <string>

#include "pdraw_utils.hpp"


namespace Pdraw
{


typedef enum
{
    FRAME_METADATA_SOURCE_RECORDING = 0,
    FRAME_METADATA_SOURCE_STREAMING,

} video_frame_metadata_source_t;


#define video_frame_metadata_t pdraw_video_frame_metadata_t
#define flying_state_t pdraw_flying_state_t
#define piloting_mode_t pdraw_piloting_mode_t
#define followme_anim_t pdraw_followme_anim_t


class VideoFrameMetadata
{
public:

    static bool decodeMetadata(const void *metadataBuffer, unsigned int metadataSize,
                               video_frame_metadata_source_t source, const char *mimeType, video_frame_metadata_t *metadata);

};

}

#endif /* !_PDRAW_METADATA_VIDEOFRAME_HPP_ */
