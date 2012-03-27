/*
 * codec_silk.h - A asterisk translation wrapper for the Skype SILK Codec
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project.
 *
 * See http://developer.skype.com/silk for more information
 * about the SILK codec, including downloads.
 *
 * Copyright (C) 2012 Todd Mortimer
 *
 * Todd Mortimer <todd.mortimer@gmail.com>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */


#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: $")

#include "asterisk/translate.h"
#include "asterisk/module.h"
#include "asterisk/format.h"
#include "asterisk/utils.h"

#include "asterisk/slin.h"
#include "asterisk/silk.h"

#include "silk/interface/SKP_Silk_SDK_API.h"
#include "ex_silk.h"

#include "asterisk/logger.h"

#define SILK_FRAME_LENGTH_MS       20
#define SILK_MAX_BYTES_PER_FRAME   1024 /* apparently.. */
#define SILK_MAX_SAMPLES_PER_FRAME 960
#define SILK_MAX_INTERNAL_FRAMES   5
#define SILK_MAX_LBRR_DELAY        2

#define SILK_BUFFER_SIZE_BYTES 5120 /* MAX_BYTES * MAX_FRAMES */
#define SLIN_BUFFER_SIZE_BYTES 9600 /* 100 ms @ 48KHZ * 2 bytes  */


SKP_int32 encSizeBytes;
SKP_int32 decSizeBytes;

/* default values */
SKP_int32 API_sampleRate = 8000;
SKP_int32 maxInternalSampleRate = 8000;
SKP_int32 packetSize_ms = SILK_FRAME_LENGTH_MS;
SKP_int32 packetLossPercentage = 0;
SKP_int32 useInBandFEC = 0;
SKP_int32 useDTX = 0;
SKP_int32 complexity = 2;
SKP_int32 bitRate = 10000;

/* translator registration indicators */
int silk8_reg = 0, silk12_reg = 0, silk16_reg = 0, silk24_reg = 0;

struct silk_coder_pvt {
  void* psEnc;
  SKP_SILK_SDK_EncControlStruct encControl;
  void* psDec;
  SKP_SILK_SDK_DecControlStruct decControl;
  int16_t buf[SLIN_BUFFER_SIZE_BYTES / 2];
};

/************ CONSTRUCTORS ************/

static int lintosilk_new(struct ast_trans_pvt *pvt)
{

  SKP_int32 ret;
  int silk_samplerate = 8000;
  int slin_samplerate = 8000;
  struct silk_coder_pvt *coder = pvt->pvt;
  char format_string[100];
  char* pointer = format_string;
  char* format;
  struct ast_format format_struct;
  struct ast_format* format_def = &format_struct;
  int attr_dtx = 0,
      attr_fec = 0,
      attr_pktloss_pct = 0,
      attr_max_bitrate = 0,
      attr_samp_rate = 0;

  /* init the silk encoder */
  coder->psEnc = malloc(encSizeBytes);
  ret = SKP_Silk_SDK_InitEncoder(coder->psEnc, &coder->encControl);
  if (ret) {
    ast_log(LOG_WARNING, "SKP_Silk_SDK_InitEncoder returned %d\n", ret);
  }

  /* Get the names of the silk codecs */
  ast_getformatname_multiple_byid(format_string, sizeof(format_string), AST_FORMAT_SILK);
  /* The destination sample rate is set explicitly */
  ret = ast_format_get_value(&pvt->explicit_dst, SILK_ATTR_KEY_SAMP_RATE, &attr_samp_rate);
  switch(attr_samp_rate){
    case SILK_ATTR_VAL_SAMP_8KHZ: silk_samplerate = 8000; break;
    case SILK_ATTR_VAL_SAMP_12KHZ: silk_samplerate = 12000; break;
    case SILK_ATTR_VAL_SAMP_16KHZ: silk_samplerate = 16000; break;
    case SILK_ATTR_VAL_SAMP_24KHZ: silk_samplerate = 24000; break;
  }
  /* get the source rate */
  switch(pvt->t->src_format.id){
    case AST_FORMAT_SLINEAR: slin_samplerate = 8000; break;
    case AST_FORMAT_SLINEAR12: slin_samplerate = 12000; break;
    case AST_FORMAT_SLINEAR16: slin_samplerate = 16000; break;
    case AST_FORMAT_SLINEAR24: slin_samplerate = 24000; break;
    default: slin_samplerate = 8000; break;
  }

  /* SILK Docs say that internal sampling rate > API Sampling rate is not allowed */
  if(slin_samplerate < silk_samplerate){
    silk_samplerate = slin_samplerate;
  }
  /* set the parameters for the coder */
  coder->encControl.API_sampleRate = (SKP_int32)slin_samplerate; /* lin input rate */
  coder->encControl.maxInternalSampleRate = (SKP_int32)silk_samplerate; /* silk output rate */
  coder->encControl.packetSize = (packetSize_ms * slin_samplerate) / 1000;
  coder->encControl.complexity = complexity;

  while ((format = strsep(&pointer, "(|)"))){
    if(strlen(format) > 0){
      if((format_def = ast_getformatbyname(format, format_def))){
        /* now pull out the format attributes */
        ret = ast_format_get_value(format_def, SILK_ATTR_KEY_DTX, &attr_dtx);
        coder->encControl.useDTX = !ret ? (SKP_int32)attr_dtx : useDTX;
        ret = ast_format_get_value(format_def, SILK_ATTR_KEY_FEC, &attr_fec);
        coder->encControl.useInBandFEC = !ret ? (SKP_int32)attr_fec : useInBandFEC;
        ret = ast_format_get_value(format_def, SILK_ATTR_KEY_PACKETLOSS_PERCENTAGE, &attr_pktloss_pct);
        coder->encControl.packetLossPercentage = !ret ? (SKP_int32)attr_pktloss_pct : packetLossPercentage;
        ret = ast_format_get_value(format_def, SILK_ATTR_KEY_MAX_BITRATE, &attr_max_bitrate);
        coder->encControl.bitRate = !ret ? (SKP_int32)attr_max_bitrate : bitRate;
        break;
      }
    }
  }

  return 0;
}

static int new_silktolin(struct ast_trans_pvt *pvt, int samplerate)
{

  SKP_int32 ret;
  struct silk_coder_pvt *coder = pvt->pvt;

  /* init the silk decoder */
  coder->psDec = malloc(decSizeBytes);
  coder->decControl.API_sampleRate = (SKP_int32)samplerate;
  coder->decControl.framesPerPacket = 1;

  /* reset decoder */
  ret = SKP_Silk_SDK_InitDecoder(coder->psDec);
  if (ret) {
    ast_log(LOG_WARNING, "SKP_Silk_SDK_InitDecoder returned %d\n", ret);
  }
  return 0;
}

static int silk8tolin_new(struct ast_trans_pvt *pvt)
{
  return new_silktolin(pvt, 8000);
}

static int silk12tolin_new(struct ast_trans_pvt *pvt)
{
  return new_silktolin(pvt, 12000);
}

static int silk16tolin_new(struct ast_trans_pvt *pvt)
{
  return new_silktolin(pvt, 16000);
}

static int silk24tolin_new(struct ast_trans_pvt *pvt)
{
  return new_silktolin(pvt, 24000);
}

/************ TRANSLATOR FUNCTIONS ************/

/* Just copy in the samples for later encoding */
static int lintosilk_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{

  struct silk_coder_pvt *coder = pvt->pvt;

  /* just add the frames to the buffer */
  memcpy(coder->buf + pvt->samples, f->data.ptr, f->datalen);
  pvt->samples += f->samples;
  return 0;
}

/* And decode everything we can in the buffer */
static struct ast_frame *lintosilk_frameout(struct ast_trans_pvt *pvt)
{

  struct silk_coder_pvt *coder = pvt->pvt;
  SKP_int ret = 0;
  SKP_int16 nBytesOut = 0;
  int datalen = 0;
  int samples = 0;
  int numPackets = 0;
  struct ast_frame *f;

  /* we can only work on multiples of a 10 ms sample
   * and no more than encControl->packetSize.
   * So we shove in packetSize samples repeatedly until
   * we are out */

  /* We only do stuff if we have more than packetSize */
  if (pvt->samples < coder->encControl.packetSize) {
    return NULL;
  }

  while (pvt->samples >= coder->encControl.packetSize) {

    nBytesOut = SILK_BUFFER_SIZE_BYTES - datalen;
    ret = SKP_Silk_SDK_Encode(coder->psEnc,
                              &coder->encControl,
                              (SKP_int16*)(coder->buf + samples),
                              coder->encControl.packetSize,
                              (SKP_uint8*)(pvt->outbuf.ui8 + datalen),
                              &nBytesOut);
    if (ret) {
      ast_log(LOG_WARNING, "Silk_Encode returned %d\n", ret);
    }

    /* nBytesOut now holds the number of bytes encoded */
    datalen += nBytesOut;
    samples += coder->encControl.packetSize;
    pvt->samples -= coder->encControl.packetSize;
    if(nBytesOut > 0){
      /* if stuff came out, we have encoded a packet */
      numPackets++;
    }
  }

  /* Move the remaining buffer stuff down */
  if (pvt->samples) {
    memmove(coder->buf, coder->buf + samples, pvt->samples *2);
  }

  if(datalen == 0){
    /* we shoved a bunch of samples in, but got no packets
     * out. We return NULL to the caller, like
     * if we could not encode anything */
    return NULL;
  }

  /* we build the frame ourselves because we have an explicit dest */
  f = &pvt->f;
  f->samples = coder->encControl.packetSize * numPackets;
  f->datalen = datalen;
  f->frametype = AST_FRAME_VOICE;
  ast_format_copy(&f->subclass.format, &pvt->explicit_dst);
  f->mallocd = 0;
  f->offset = AST_FRIENDLY_OFFSET;
  f->src = pvt->t->name;
  f->data.ptr = pvt->outbuf.c;
  return ast_frisolate(f);
}

static int silktolin_framein(struct ast_trans_pvt *pvt, struct ast_frame *f)
{

  struct silk_coder_pvt *coder = pvt->pvt;
  SKP_int   ret = 0;
  SKP_int16 numSamplesOut = 0;
  SKP_int16 totalSamplesOut = 0;
  SKP_int16 *dst = (SKP_int16*)pvt->outbuf.i16;
  SKP_uint8 *src = (SKP_uint8*)f->data.ptr;
  SKP_int   srcBytes = (SKP_int)f->datalen;
  SKP_int   lostFlag = 0; /* assume no loss for now */
  int       decodeIterations = SILK_MAX_INTERNAL_FRAMES;
  /*
  int       i = 0;
  struct ast_frame *nf = NULL;
  SKP_uint8 FECPayload[SILK_MAX_BYTES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES];
  SKP_int16 FECBytes = 0;
  */

  /* If we indicate native PLC in the translator for silktolin
   * then we may get passed an empty frame (f->datalen = 0), indicating
   * that we should fill in some PLC data. So when we get f->datalen=0
   * we should check to see if we have any next frames (check
   * f->frame_list.next ?) and look inside them with
   * SKP_Silk_SDK_search_for_LBRR(). We can search up to 2 frames ahead.
   * If we find LBRR data, then we should pass that data to the
   * decoder. If we do not find any LBRR data, then we should
   * just pass lostFlag=1 to the decoder for normal PLC */

  if(srcBytes == 0) { /* Native PLC */
    lostFlag = 1;
    /* search for LBRR data */
    /* FIXME: Actually do the lookahead, which I guess will require a jitterbuffer? */
    /*
    if (f->frame_list && f->frame_list.next) {
      nf = f->frame_list.next;
    }
    for(i = 0; i < SILK_MAX_LBRR_DELAY; ++i){
      if(nf) {
        if(nf->datalen) {
          SKP_Silk_SDK_search_for_LBRR((SKP_uint8*)nf->data.ptr,
                                       nf->datalen,
                                       i+1,
                                       FECPayload,
                                       &FECBytes);
          if(FECBytes > 0){
            src = FECPayload;
            srcBytes = FECBytes;
            lostFlag = 0;
            break;
          }
        }
        if (nf->frame_list && nf->frame_list.next) {
          nf = nf->frame_list.next;
        }
      }
    }
    */
  }

  if(lostFlag) {
    /* set the decodeIterations for the do{}while() to be the
     * number of frames in the last decoded packet */
    decodeIterations = coder->decControl.framesPerPacket;
    ast_log(LOG_NOTICE, "silktolin indicated lost packet - no LBRR");
  }

  do {
    ret = SKP_Silk_SDK_Decode(coder->psDec,
                              &coder->decControl,
                              lostFlag,
                              src,
                              srcBytes,
                              dst,
                              &numSamplesOut);

    if(ret) {
      ast_log(LOG_NOTICE, "SKP_Silk_SDK_Decode returned %d\n", ret);
    }

    dst += numSamplesOut;
    totalSamplesOut += numSamplesOut;

    /* decrement the number of iterations remaining */
    decodeIterations--;

  /* while we have more data and have not gone too far */
  } while (coder->decControl.moreInternalDecoderFrames
            && decodeIterations > 0);

  /* okay, we've decoded everything we can */
  pvt->samples = totalSamplesOut;
  pvt->datalen = totalSamplesOut * 2;
  return 0;

}

/************ DESTRUCTORS ************/

static void lintosilk_destroy(struct ast_trans_pvt *pvt)
{

  struct silk_coder_pvt *coder = pvt->pvt;

  // free the memory we allocated for the encoder
  free(coder->psEnc);
}


static void silktolin_destroy(struct ast_trans_pvt *pvt)
{

  struct silk_coder_pvt *coder = pvt->pvt;
  free(coder->psDec);

}

/************ TRANSLATOR DEFINITIONS ************/


static struct ast_translator silk8tolin = {
  .name = "silk8tolin",
  .newpvt = silk8tolin_new,
  .framein = silktolin_framein,
  .destroy = silktolin_destroy,
  .sample = silk8_sample,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
  .buf_size = SLIN_BUFFER_SIZE_BYTES,
  .native_plc = 1
};

static struct ast_translator lintosilk8 = {
  .name = "lintosilk8",
  .newpvt = lintosilk_new,
  .framein = lintosilk_framein,
  .frameout = lintosilk_frameout,
  .destroy = lintosilk_destroy,
  .sample = slin8_sample,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
  .buf_size = SILK_BUFFER_SIZE_BYTES
};

static struct ast_translator silk12tolin = {
  .name = "silk12tolin",
  .newpvt = silk12tolin_new,
  .framein = silktolin_framein,
  .destroy = silktolin_destroy,
  .sample = silk12_sample,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
  .buf_size = SLIN_BUFFER_SIZE_BYTES,
  .native_plc = 1
};

static struct ast_translator lintosilk12 = {
  .name = "lintosilk12",
  .newpvt = lintosilk_new,
  .framein = lintosilk_framein,
  .frameout = lintosilk_frameout,
  .destroy = lintosilk_destroy,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
  .buf_size = SILK_BUFFER_SIZE_BYTES
};

static struct ast_translator silk16tolin = {
  .name = "silk16tolin",
  .newpvt = silk16tolin_new,
  .framein = silktolin_framein,
  .destroy = silktolin_destroy,
  .sample = silk16_sample,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
  .buf_size = SLIN_BUFFER_SIZE_BYTES,
  .native_plc = 1
};

static struct ast_translator lintosilk16 = {
  .name = "lintosilk16",
  .newpvt = lintosilk_new,
  .framein = lintosilk_framein,
  .frameout = lintosilk_frameout,
  .destroy = lintosilk_destroy,
  .sample = slin16_sample,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
  .buf_size = SILK_BUFFER_SIZE_BYTES
};

static struct ast_translator silk24tolin = {
  .name = "silk24tolin",
  .newpvt = silk24tolin_new,
  .framein = silktolin_framein,
  .destroy = silktolin_destroy,
  .sample = silk24_sample,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SLIN_BUFFER_SIZE_BYTES / 2,
  .buf_size = SLIN_BUFFER_SIZE_BYTES,
  .native_plc = 1
};

static struct ast_translator lintosilk24 = {
  .name = "lintosilk24",
  .newpvt = lintosilk_new,
  .framein = lintosilk_framein,
  .frameout = lintosilk_frameout,
  .destroy = lintosilk_destroy,
  .desc_size = sizeof(struct silk_coder_pvt),
  .buffer_samples = SILK_MAX_SAMPLES_PER_FRAME * SILK_MAX_INTERNAL_FRAMES,
  .buf_size = SILK_BUFFER_SIZE_BYTES
};



/************ MODULE LOAD / UNLOAD **************/

static int load_module(void)
{
  SKP_int32 ret = 0;;
  int res = 0;
  char format_string[100];
  char* format;
  char* pointer = format_string;
  struct ast_format format_struct;
  struct ast_format* format_def = &format_struct;
  int attr_samp_rate = 0;

  /* print the skype version */
  ast_log(LOG_NOTICE, "SILK Version : %s\n", SKP_Silk_SDK_get_version());

  /* get the encoder / decoder sizes */
  ret = SKP_Silk_SDK_Get_Encoder_Size(&encSizeBytes);
  if (ret) {
    ast_log(LOG_WARNING, "SKP_Silk_SDK_Get_Encoder_size returned %d", ret);
  }
  ret = SKP_Silk_SDK_Get_Decoder_Size(&decSizeBytes);
  if (ret) {
    ast_log(LOG_WARNING, "SKP_Silk_SDK_Get_Decoder_size returned %d", ret);
  }

  /* Finish the setup of the encoders / decoders */
  /* silk8 */
  ast_format_set(&silk8tolin.src_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_8KHZ,
                AST_FORMAT_ATTR_END);
  ast_format_set(&silk8tolin.dst_format, AST_FORMAT_SLINEAR,0);

  ast_format_set(&lintosilk8.src_format, AST_FORMAT_SLINEAR,0);
  ast_format_set(&lintosilk8.dst_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_8KHZ,
                AST_FORMAT_ATTR_END);
  /* silk12 */
  ast_format_set(&silk12tolin.src_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_12KHZ,
                AST_FORMAT_ATTR_END);
  ast_format_set(&silk12tolin.dst_format, AST_FORMAT_SLINEAR12,0);

  ast_format_set(&lintosilk12.src_format, AST_FORMAT_SLINEAR12,0);
  ast_format_set(&lintosilk12.dst_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_12KHZ,
                AST_FORMAT_ATTR_END);

  /* silk16 */
  ast_format_set(&silk16tolin.src_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_16KHZ,
                AST_FORMAT_ATTR_END);
  ast_format_set(&silk16tolin.dst_format, AST_FORMAT_SLINEAR16,0);

  ast_format_set(&lintosilk16.src_format, AST_FORMAT_SLINEAR16,0);
  ast_format_set(&lintosilk16.dst_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_16KHZ,
                AST_FORMAT_ATTR_END);

  /* silk24 */
  ast_format_set(&silk24tolin.src_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_24KHZ,
                AST_FORMAT_ATTR_END);
  ast_format_set(&silk24tolin.dst_format, AST_FORMAT_SLINEAR24,0);

  ast_format_set(&lintosilk24.src_format, AST_FORMAT_SLINEAR24,0);
  ast_format_set(&lintosilk24.dst_format, AST_FORMAT_SILK,1,
                SILK_ATTR_KEY_SAMP_RATE,
                SILK_ATTR_VAL_SAMP_24KHZ,
                AST_FORMAT_ATTR_END);


  /* Get the names of the silk codecs */
  ast_getformatname_multiple_byid(format_string, sizeof(format_string), AST_FORMAT_SILK);
  ast_log(LOG_NOTICE, "Defined silk codecs are: %s\n", format_string);

  while ((format = strsep(&pointer, "(|)"))){
    if(strlen(format) > 0){
      if((format_def = ast_getformatbyname(format, format_def))){
        /* now pull out the format attributes */
        ret = ast_format_get_value(format_def, SILK_ATTR_KEY_SAMP_RATE, &attr_samp_rate);
        if(!ret) {
          switch (attr_samp_rate) {
            case SILK_ATTR_VAL_SAMP_8KHZ:
              res |= ast_register_translator(&silk8tolin);
              res |= ast_register_translator(&lintosilk8);
              silk8_reg = 1;
              break;
            case SILK_ATTR_VAL_SAMP_12KHZ:
              res |= ast_register_translator(&silk12tolin);
              res |= ast_register_translator(&lintosilk12);
              silk12_reg = 1;
              break;
            case SILK_ATTR_VAL_SAMP_16KHZ:
              res |= ast_register_translator(&silk16tolin);
              res |= ast_register_translator(&lintosilk16);
              silk16_reg = 1;
              break;
            case SILK_ATTR_VAL_SAMP_24KHZ:
              res |= ast_register_translator(&silk24tolin);
              res |= ast_register_translator(&lintosilk24);
              silk24_reg = 1;
              break;
          }
        }
      }
    }
  }


  /* register the encoder / decoder */
  if(res){
    return AST_MODULE_LOAD_FAILURE;
  }

  return AST_MODULE_LOAD_SUCCESS;
}

static int unload_module(void)
{
  int res = 0;
  ast_log(LOG_NOTICE, "Silk Coder/Encoder unloading\n");

  if(silk8_reg){
    res |= ast_unregister_translator(&lintosilk8);
    res |= ast_unregister_translator(&silk8tolin);
  }
  if (silk12_reg){
    res |= ast_unregister_translator(&lintosilk12);
    res |= ast_unregister_translator(&silk12tolin);
  }
  if(silk16_reg){
    res |= ast_unregister_translator(&lintosilk16);
    res |= ast_unregister_translator(&silk16tolin);
  }
  if(silk24_reg){
    res |= ast_unregister_translator(&lintosilk24);
    res |= ast_unregister_translator(&silk24tolin);
  }
  return res;
}


AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "SILK Coder/Decoder");

