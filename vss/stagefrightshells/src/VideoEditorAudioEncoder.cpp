/*
 * Copyright (C) 2011 NXP Software
 * Copyright (C) 2011 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
/**
*************************************************************************
* @file   VideoEditorAudioEncoder.cpp
* @brief  StageFright shell Audio Encoder
*************************************************************************
*/

#define LOG_NDEBUG 1
#define LOG_TAG "VIDEOEDITOR_AUDIOENCODER"

#include "M4OSA_Debug.h"
#include "VideoEditorAudioEncoder.h"
#include "VideoEditorUtils.h"

#include "utils/Log.h"
#include <media/stagefright/MediaSource.h>
#include <media/stagefright/MediaDebug.h>
#include <media/stagefright/MediaDefs.h>
#include <media/stagefright/MetaData.h>
#include <media/stagefright/OMXClient.h>
#include <media/stagefright/OMXCodec.h>

/*** DEFINITIONS ***/
// Force using software encoder as engine does not support prefetch
#define VIDEOEDITOR_FORCECODEC kSoftwareCodecsOnly

namespace android {
struct VideoEditorAudioEncoderSource : public MediaSource {
    public:
        static sp<VideoEditorAudioEncoderSource> Create();
        virtual status_t start(MetaData *params = NULL);
        virtual status_t stop();
        virtual sp<MetaData> getFormat();
        virtual status_t read(MediaBuffer **buffer,
        const ReadOptions *options = NULL);
        virtual int32_t storeBuffer(MediaBuffer *buffer);

    protected:
        virtual ~VideoEditorAudioEncoderSource();

    private:
        struct MediaBufferChain {
            MediaBuffer* buffer;
            MediaBufferChain* nextLink;
        };
        enum State {
            CREATED,
            STARTED,
            ERROR
        };
        VideoEditorAudioEncoderSource();
        MediaBufferChain* mFirstBufferLink;
        MediaBufferChain* mLastBufferLink;
        int32_t mNbBuffer;
        State mState;
};

sp<VideoEditorAudioEncoderSource> VideoEditorAudioEncoderSource::Create() {

    LOGV("VideoEditorAudioEncoderSource::Create");
    sp<VideoEditorAudioEncoderSource> aSource =
        new VideoEditorAudioEncoderSource();

    return aSource;
}

VideoEditorAudioEncoderSource::VideoEditorAudioEncoderSource():
        mFirstBufferLink(NULL),
        mLastBufferLink(NULL),
        mNbBuffer(0),
        mState(CREATED) {
    LOGV("VideoEditorAudioEncoderSource::VideoEditorAudioEncoderSource");
}


VideoEditorAudioEncoderSource::~VideoEditorAudioEncoderSource() {
    LOGV("VideoEditorAudioEncoderSource::~VideoEditorAudioEncoderSource");

    if( STARTED == mState ) {
        stop();
    }
}

status_t VideoEditorAudioEncoderSource::start(MetaData *meta) {
    status_t err = OK;

    LOGV("VideoEditorAudioEncoderSource::start");

    if( CREATED != mState ) {
        LOGV("VideoEditorAudioEncoderSource::start ERROR : invalid state %d",
            mState);
        return UNKNOWN_ERROR;
    }

    mState = STARTED;

cleanUp:
    LOGV("VideoEditorAudioEncoderSource::start END (0x%x)", err);
    return err;
}

status_t VideoEditorAudioEncoderSource::stop() {
    status_t err = OK;

    LOGV("VideoEditorAudioEncoderSource::stop");

    if( STARTED != mState ) {
        LOGV("VideoEditorAudioEncoderSource::stop ERROR: invalid state %d",
            mState);
        return UNKNOWN_ERROR;
    }

    int32_t i = 0;
    MediaBufferChain* tmpLink = NULL;
    while( mFirstBufferLink ) {
        i++;
        tmpLink = mFirstBufferLink;
        mFirstBufferLink = mFirstBufferLink->nextLink;
        delete tmpLink;
    }
    LOGV("VideoEditorAudioEncoderSource::stop : %d buffer remained", i);
    mFirstBufferLink = NULL;
    mLastBufferLink = NULL;

    mState = CREATED;

    LOGV("VideoEditorAudioEncoderSource::stop END (0x%x)", err);
    return err;
}

sp<MetaData> VideoEditorAudioEncoderSource::getFormat() {
    LOGV("VideoEditorAudioEncoderSource::getFormat");

   LOGV("VideoEditorAudioEncoderSource::getFormat :THIS IS NOT IMPLEMENTED");

    return NULL;
}

status_t VideoEditorAudioEncoderSource::read(MediaBuffer **buffer,
        const ReadOptions *options) {
    MediaSource::ReadOptions readOptions;
    status_t err = OK;
    MediaBufferChain* tmpLink = NULL;

    LOGV("VideoEditorAudioEncoderSource::read");

    if ( STARTED != mState ) {
        LOGV("VideoEditorAudioEncoderSource::read ERROR : invalid state %d",
            mState);
        return UNKNOWN_ERROR;
    }

    if( NULL == mFirstBufferLink ) {
        *buffer = NULL;
        LOGV("VideoEditorAudioEncoderSource::read : EOS");
        return ERROR_END_OF_STREAM;
    }
    *buffer = mFirstBufferLink->buffer;

    tmpLink = mFirstBufferLink;
    mFirstBufferLink = mFirstBufferLink->nextLink;
    if( NULL == mFirstBufferLink ) {
        mLastBufferLink = NULL;
    }
    delete tmpLink;
    mNbBuffer--;

    LOGV("VideoEditorAudioEncoderSource::read END (0x%x)", err);
    return err;
}

int32_t VideoEditorAudioEncoderSource::storeBuffer(MediaBuffer *buffer) {
    status_t err = OK;

    LOGV("VideoEditorAudioEncoderSource::storeBuffer");

    MediaBufferChain* newLink = new MediaBufferChain;
    newLink->buffer = buffer;
    newLink->nextLink = NULL;
    if( NULL != mLastBufferLink ) {
        mLastBufferLink->nextLink = newLink;
    } else {
        mFirstBufferLink = newLink;
    }
    mLastBufferLink = newLink;
    mNbBuffer++;

    LOGV("VideoEditorAudioEncoderSource::storeBuffer END");
    return mNbBuffer;
}

/********************
 * ENGINE INTERFACE *
 ********************/
/**
 ******************************************************************************
 * structure VideoEditorAudioEncoder_Context
 * @brief    This structure defines the context of the StageFright audio
 *           encoder shell
 ******************************************************************************
*/
typedef struct {
    M4ENCODER_AudioFormat             mFormat;
    M4ENCODER_AudioParams*            mCodecParams;
    M4ENCODER_AudioDecSpecificInfo    mDSI;
    sp<VideoEditorAudioEncoderSource> mEncoderSource;
    OMXClient                         mClient;
    sp<MediaSource>                   mEncoder;
    uint32_t                          mNbInputFrames;
    uint32_t                          mNbOutputFrames;
    int64_t                           mFirstOutputCts;
    int64_t                           mLastOutputCts;
} VideoEditorAudioEncoder_Context;

M4OSA_ERR VideoEditorAudioEncoder_cleanup(M4OSA_Context pContext) {

    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;

    LOGV("VideoEditorAudioEncoder_cleanup begin");
    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext, M4ERR_PARAMETER);
    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;

    SAFE_FREE(pEncoderContext->mDSI.pInfo);
    SAFE_FREE(pEncoderContext);
    pContext = M4OSA_NULL;

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_cleanup no error");
    } else {
        LOGV("VideoEditorAudioEncoder_cleanup ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_cleanup end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_init(M4ENCODER_AudioFormat format,
        M4OSA_Context* pContext, M4OSA_Void* pUserData) {

    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;

    LOGV(" VideoEditorAudioEncoder_init begin: format %d", format);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext, M4ERR_PARAMETER);

    SAFE_MALLOC(pEncoderContext, VideoEditorAudioEncoder_Context, 1,
        "VideoEditorAudioEncoder");
    pEncoderContext->mFormat = format;

    *pContext = pEncoderContext;

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_init no error");
    } else {
        VideoEditorAudioEncoder_cleanup(pEncoderContext);
        *pContext = M4OSA_NULL;
        LOGV("VideoEditorAudioEncoder_init ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_init end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_init_AAC(M4OSA_Context* pContext,
        M4OSA_Void* pUserData) {
    return VideoEditorAudioEncoder_init(M4ENCODER_kAAC, pContext, pUserData);
}

M4OSA_ERR VideoEditorAudioEncoder_init_AMRNB(M4OSA_Context* pContext,
        M4OSA_Void* pUserData) {
    return VideoEditorAudioEncoder_init(M4ENCODER_kAMRNB, pContext, pUserData);
}

M4OSA_ERR VideoEditorAudioEncoder_init_MP3(M4OSA_Context* pContext,
        M4OSA_Void* pUserData) {
    return VideoEditorAudioEncoder_init(M4ENCODER_kMP3, pContext, pUserData);
}

M4OSA_ERR VideoEditorAudioEncoder_close(M4OSA_Context pContext) {

    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;

    LOGV("VideoEditorAudioEncoder_close begin");

    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext, M4ERR_PARAMETER);
    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;

    SAFE_FREE(pEncoderContext->mCodecParams);

    pEncoderContext->mEncoder->stop();
    pEncoderContext->mEncoder.clear();
    pEncoderContext->mClient.disconnect();
    pEncoderContext->mEncoderSource.clear();

    LOGV("AudioEncoder_close:IN %d frames,OUT %d frames from %lld to %lld",
        pEncoderContext->mNbInputFrames,
        pEncoderContext->mNbOutputFrames, pEncoderContext->mFirstOutputCts,
        pEncoderContext->mLastOutputCts);

    if( pEncoderContext->mNbInputFrames != pEncoderContext->mNbInputFrames ) {
        LOGV("VideoEditorAudioEncoder_close:some frames were not encoded %d %d",
            pEncoderContext->mNbInputFrames, pEncoderContext->mNbInputFrames);
    }

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_close no error");
    } else {
        LOGV("VideoEditorAudioEncoder_close ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_close begin end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_open(M4OSA_Context pContext,
        M4ENCODER_AudioParams *pParams, M4ENCODER_AudioDecSpecificInfo *pDSI,
        M4OSA_Context pGrabberContext) {

    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;
    status_t result = OK;
    sp<MetaData> encoderMetadata = NULL;
    const char* mime = NULL;
    int32_t iNbChannel = 0;
    uint32_t codecFlags = 0;

    LOGV("VideoEditorAudioEncoder_open begin");

    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext, M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pParams,  M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pDSI,     M4ERR_PARAMETER);

    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;
    pDSI->pInfo = M4OSA_NULL;
    pDSI->infoSize = 0;

    pEncoderContext->mNbInputFrames  = 0;
    pEncoderContext->mNbOutputFrames = 0;
    pEncoderContext->mFirstOutputCts = -1;
    pEncoderContext->mLastOutputCts  = -1;

    // Allocate & initialize the encoding parameters
    LOGV("VideoEditorAudioEncoder_open : params F=%d CN=%d BR=%d F=%d",
        pParams->Frequency, pParams->ChannelNum, pParams->Bitrate,
        pParams->Format);
    SAFE_MALLOC(pEncoderContext->mCodecParams, M4ENCODER_AudioParams, 1,
        "VIDEOEDITOR CodecParams");
    pEncoderContext->mCodecParams->Frequency  = pParams->Frequency;
    pEncoderContext->mCodecParams->ChannelNum = pParams->ChannelNum;
    pEncoderContext->mCodecParams->Bitrate    = pParams->Bitrate;
    pEncoderContext->mCodecParams->Format     = pParams->Format;

    // Check output format consistency
    VIDEOEDITOR_CHECK(pEncoderContext->mCodecParams->Format ==
        pEncoderContext->mFormat, M4ERR_PARAMETER);

    /**
     * StageFright graph building
     */
    // Create the meta data for the encoder
    encoderMetadata = new MetaData;
    switch( pEncoderContext->mCodecParams->Format ) {
        case M4ENCODER_kAAC:
        {
            mime = MEDIA_MIMETYPE_AUDIO_AAC;
            break;
        }
        case M4ENCODER_kAMRNB:
        {
            mime = MEDIA_MIMETYPE_AUDIO_AMR_NB;
            break;
        }
        default:
        {
            VIDEOEDITOR_CHECK(!"AudioEncoder_open : incorrect input format",
            M4ERR_PARAMETER);
            break;
        }
    }
    encoderMetadata->setCString(kKeyMIMEType, mime);
    encoderMetadata->setInt32(kKeySampleRate,
        (int32_t)pEncoderContext->mCodecParams->Frequency);
    encoderMetadata->setInt32(kKeyBitRate,
        (int32_t)pEncoderContext->mCodecParams->Bitrate);

    switch( pEncoderContext->mCodecParams->ChannelNum ) {
        case M4ENCODER_kMono:
        {
            iNbChannel = 1;
            break;
        }
        case M4ENCODER_kStereo:
        {
            iNbChannel = 2;
            break;
        }
        default:
        {
            VIDEOEDITOR_CHECK(!"AudioEncoder_open : incorrect channel number",
                M4ERR_STATE);
            break;
        }
    }
    encoderMetadata->setInt32(kKeyChannelCount, iNbChannel);

    // Create the encoder source
    pEncoderContext->mEncoderSource = VideoEditorAudioEncoderSource::Create();
    VIDEOEDITOR_CHECK(NULL != pEncoderContext->mEncoderSource.get(),
        M4ERR_STATE);

    // Connect to the OMX client
    result = pEncoderContext->mClient.connect();
    VIDEOEDITOR_CHECK(OK == result, M4ERR_STATE);

    // Create the OMX codec
#ifdef VIDEOEDITOR_FORCECODEC
    codecFlags |= OMXCodec::VIDEOEDITOR_FORCECODEC;
#endif /* VIDEOEDITOR_FORCECODEC */
    pEncoderContext->mEncoder = OMXCodec::Create(
        pEncoderContext->mClient.interface(), encoderMetadata, true,
        pEncoderContext->mEncoderSource, NULL, codecFlags);
    VIDEOEDITOR_CHECK(NULL != pEncoderContext->mEncoder.get(), M4ERR_STATE);

    // Start the graph
    result = pEncoderContext->mEncoder->start();
    VIDEOEDITOR_CHECK(OK == result, M4ERR_STATE);

    // Get AAC DSI, this code can only work with software encoder
    if( M4ENCODER_kAAC == pEncoderContext->mCodecParams->Format ) {
        int32_t      isCodecConfig = 0;
        MediaBuffer* buffer        = NULL;

        // Read once to get the DSI
        result = pEncoderContext->mEncoder->read(&buffer, NULL);
        VIDEOEDITOR_CHECK(OK == result, M4ERR_STATE);
        VIDEOEDITOR_CHECK(buffer->meta_data()->findInt32(kKeyIsCodecConfig,
            &isCodecConfig) && isCodecConfig, M4ERR_STATE);

        // Save the DSI
        pEncoderContext->mDSI.infoSize = (M4OSA_UInt32)buffer->range_length();
        SAFE_MALLOC(pEncoderContext->mDSI.pInfo, M4OSA_Int8,
            pEncoderContext->mDSI.infoSize, "Encoder header");

        memcpy((void *)pEncoderContext->mDSI.pInfo,
            (void *)((M4OSA_MemAddr8)(buffer->data())+buffer->range_offset()),
            pEncoderContext->mDSI.infoSize);

        buffer->release();
        *pDSI = pEncoderContext->mDSI;
    }
    LOGV("VideoEditorAudioEncoder_open : DONE");

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_open no error");
    } else {
        VideoEditorAudioEncoder_close(pEncoderContext);
        LOGV("VideoEditorAudioEncoder_open ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_open end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_processInputBuffer(M4OSA_Context pContext,
        M4ENCODER_AudioBuffer* pInBuffer) {

    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;
    M4OSA_Int8* pData = M4OSA_NULL;
    MediaBuffer* buffer = NULL;
    int32_t nbBuffer = 0;

    LOGV("VideoEditorAudioEncoder_processInputBuffer begin");
    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext, M4ERR_PARAMETER);

    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;

    switch( pEncoderContext->mCodecParams->ChannelNum ) {
        case M4ENCODER_kMono:
        case M4ENCODER_kStereo:
            // Let the MediaBuffer own the data so we don't have to free it
            buffer = new MediaBuffer((size_t)pInBuffer->pTableBufferSize[0]);
            pData = (M4OSA_Int8*)buffer->data() + buffer->range_offset();
            memcpy((void *)pData, (void *)pInBuffer->pTableBuffer[0],
                pInBuffer->pTableBufferSize[0]);
            break;
        default:
            LOGV("VEAE_processInputBuffer unsupported channel configuration %d",
                pEncoderContext->mCodecParams->ChannelNum);
            VIDEOEDITOR_CHECK(M4OSA_FALSE, M4ERR_PARAMETER);
            break;
    }

    LOGV("VideoEditorAudioEncoder_processInputBuffer : store %d bytes",
        buffer->range_length());
    // Push the buffer to the source
    nbBuffer = pEncoderContext->mEncoderSource->storeBuffer(buffer);

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_processInputBuffer no error");
    } else {
        if( NULL != buffer ) {
            buffer->release();
        }
        LOGV("VideoEditorAudioEncoder_processInputBuffer ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_processInputBuffer end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_processOutputBuffer(M4OSA_Context pContext,
        MediaBuffer* buffer, M4ENCODER_AudioBuffer* pOutBuffer) {

    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;
    M4OSA_UInt32 Cts = 0;
    int32_t i32Tmp = 0;
    int64_t i64Tmp = 0;
    status_t result = OK;

    LOGV("VideoEditorAudioEncoder_processOutputBuffer begin");
    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext,   M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != buffer,     M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pOutBuffer, M4ERR_PARAMETER);

    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;

    // Process the returned AU
    if( 0 == buffer->range_length() ) {
        // Encoder has no data yet, nothing unusual
        LOGV("VideoEditorAudioEncoder_processOutputBuffer : buffer is empty");
        pOutBuffer->pTableBufferSize[0] = 0;
        goto cleanUp;
    }
    if( buffer->meta_data()->findInt32(kKeyIsCodecConfig, &i32Tmp) && i32Tmp ) {
        /* This should not happen with software encoder,
         * DSI was retrieved beforehand */
        VIDEOEDITOR_CHECK(M4OSA_FALSE, M4ERR_STATE);
    } else {
        // Check the CTS
        VIDEOEDITOR_CHECK(buffer->meta_data()->findInt64(kKeyTime, &i64Tmp),
            M4ERR_STATE);
        Cts = (M4OSA_Int32)(i64Tmp/1000);

        pEncoderContext->mNbOutputFrames++;
        if( 0 > pEncoderContext->mFirstOutputCts ) {
            pEncoderContext->mFirstOutputCts = i64Tmp;
        }
        pEncoderContext->mLastOutputCts = i64Tmp;

        // Format the AU
        memcpy((void *)pOutBuffer->pTableBuffer[0],
            (void *)((M4OSA_MemAddr8)(buffer->data())+buffer->range_offset()),
            buffer->range_length());
        pOutBuffer->pTableBufferSize[0] = (M4OSA_UInt32)buffer->range_length();
    }

cleanUp:
    // Release the buffer
    buffer->release();
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_processOutputBuffer no error");
    } else {
        LOGV("VideoEditorAudioEncoder_processOutputBuffer ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_processOutputBuffer end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_step(M4OSA_Context pContext,
        M4ENCODER_AudioBuffer* pInBuffer, M4ENCODER_AudioBuffer* pOutBuffer) {
    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;
    status_t result = OK;
    MediaBuffer* buffer = NULL;

    LOGV("VideoEditorAudioEncoder_step begin");

    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext,   M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pInBuffer,  M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pOutBuffer, M4ERR_PARAMETER);

    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;
    pEncoderContext->mNbInputFrames++;

    // Push the input buffer to the encoder source
    err = VideoEditorAudioEncoder_processInputBuffer(pEncoderContext,pInBuffer);
    VIDEOEDITOR_CHECK(M4NO_ERROR == err, err);

    // Read
    result = pEncoderContext->mEncoder->read(&buffer, NULL);
    VIDEOEDITOR_CHECK(OK == result, M4ERR_STATE);

    // Provide the encoded AU to the writer
    err = VideoEditorAudioEncoder_processOutputBuffer(pEncoderContext, buffer,
        pOutBuffer);
    VIDEOEDITOR_CHECK(M4NO_ERROR == err, err);

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_step no error");
    } else {
        LOGV("VideoEditorAudioEncoder_step ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_step end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_getOption(M4OSA_Context pContext,
        M4OSA_OptionID optionID, M4OSA_DataOption* optionValue) {
    M4OSA_ERR err = M4NO_ERROR;
    VideoEditorAudioEncoder_Context* pEncoderContext = M4OSA_NULL;

    LOGV("VideoEditorAudioEncoder_getOption begin optionID 0x%X", optionID);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pContext, M4ERR_PARAMETER);

    pEncoderContext = (VideoEditorAudioEncoder_Context*)pContext;

    switch( optionID ) {
        default:
            LOGV("VideoEditorAudioEncoder_getOption: unsupported optionId 0x%X",
                optionID);
            VIDEOEDITOR_CHECK(M4OSA_FALSE, M4ERR_BAD_OPTION_ID);
            break;
    }

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_getOption no error");
    } else {
        LOGV("VideoEditorAudioEncoder_getOption ERROR 0x%X", err);
    }
    LOGV("VideoEditorAudioEncoder_getOption end");
    return err;
}

M4OSA_ERR VideoEditorAudioEncoder_getInterface(
        M4ENCODER_AudioFormat format, M4ENCODER_AudioFormat* pFormat,
        M4ENCODER_AudioGlobalInterface** pEncoderInterface) {
    M4OSA_ERR err = M4NO_ERROR;

    // Input parameters check
    VIDEOEDITOR_CHECK(M4OSA_NULL != pFormat,           M4ERR_PARAMETER);
    VIDEOEDITOR_CHECK(M4OSA_NULL != pEncoderInterface, M4ERR_PARAMETER);

    LOGV("VideoEditorAudioEncoder_getInterface 0x%x 0x%x",pFormat,
        pEncoderInterface);
    SAFE_MALLOC(*pEncoderInterface, M4ENCODER_AudioGlobalInterface, 1,
        "AudioEncoder");

    *pFormat = format;

    switch( format ) {
        case M4ENCODER_kAAC:
        {
            (*pEncoderInterface)->pFctInit = VideoEditorAudioEncoder_init_AAC;
            break;
        }
        case M4ENCODER_kAMRNB:
        {
            (*pEncoderInterface)->pFctInit = VideoEditorAudioEncoder_init_AMRNB;
            break;
        }
        case M4ENCODER_kMP3:
        {
            (*pEncoderInterface)->pFctInit = VideoEditorAudioEncoder_init_MP3;
            break;
        }
        default:
        {
            LOGV("VideoEditorAudioEncoder_getInterface: unsupported format %d",
                format);
            VIDEOEDITOR_CHECK(M4OSA_FALSE, M4ERR_PARAMETER);
        break;
        }
    }
    (*pEncoderInterface)->pFctCleanUp      = VideoEditorAudioEncoder_cleanup;
    (*pEncoderInterface)->pFctOpen         = VideoEditorAudioEncoder_open;
    (*pEncoderInterface)->pFctClose        = VideoEditorAudioEncoder_close;
    (*pEncoderInterface)->pFctStep         = VideoEditorAudioEncoder_step;
    (*pEncoderInterface)->pFctGetOption    = VideoEditorAudioEncoder_getOption;

cleanUp:
    if( M4NO_ERROR == err ) {
        LOGV("VideoEditorAudioEncoder_getInterface no error");
    } else {
        *pEncoderInterface = M4OSA_NULL;
        LOGV("VideoEditorAudioEncoder_getInterface ERROR 0x%X", err);
    }
    return err;
}
extern "C" {

M4OSA_ERR VideoEditorAudioEncoder_getInterface_AAC(
        M4ENCODER_AudioFormat* pFormat,
        M4ENCODER_AudioGlobalInterface** pEncoderInterface) {
    return VideoEditorAudioEncoder_getInterface(
        M4ENCODER_kAAC, pFormat, pEncoderInterface);
}

M4OSA_ERR VideoEditorAudioEncoder_getInterface_AMRNB(
        M4ENCODER_AudioFormat* pFormat,
        M4ENCODER_AudioGlobalInterface** pEncoderInterface) {

    return VideoEditorAudioEncoder_getInterface(
        M4ENCODER_kAMRNB, pFormat, pEncoderInterface);
}

M4OSA_ERR VideoEditorAudioEncoder_getInterface_MP3(
        M4ENCODER_AudioFormat* pFormat,
        M4ENCODER_AudioGlobalInterface** pEncoderInterface) {
    LOGV("VideoEditorAudioEncoder_getInterface_MP3 no error");

    return VideoEditorAudioEncoder_getInterface(
        M4ENCODER_kMP3, pFormat, pEncoderInterface);
}

}  // extern "C"

}  // namespace android
