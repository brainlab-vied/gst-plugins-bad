/* -LICENSE-START-
** Copyright (c) 2009 Blackmagic Design
**
** Permission is hereby granted, free of charge, to any person or organization
** obtaining a copy of the software and accompanying documentation covered by
** this license (the "Software") to use, reproduce, display, distribute,
** execute, and transmit the Software, and to prepare derivative works of the
** Software, and to permit third-parties to whom the Software is furnished to
** do so, all subject to the following:
** 
** The copyright notices in the Software and this entire statement, including
** the above license grant, this restriction and the following disclaimer,
** must be included in all copies of the Software, in whole or in part, and
** all derivative works of the Software, unless such copies or derivative
** works are solely in the form of machine-executable object code generated by
** a source language processor.
** 
** THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
** IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
** FITNESS FOR A PARTICULAR PURPOSE, TITLE AND NON-INFRINGEMENT. IN NO EVENT
** SHALL THE COPYRIGHT HOLDERS OR ANYONE DISTRIBUTING THE SOFTWARE BE LIABLE
** FOR ANY DAMAGES OR OTHER LIABILITY, WHETHER IN CONTRACT, TORT OR OTHERWISE,
** ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
** DEALINGS IN THE SOFTWARE.
** -LICENSE-END-
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "gstdecklinksrc.h"

#include "capture.h"

#define GST_CAT_DEFAULT gst_decklink_src_debug_category

static BMDTimecodeFormat g_timecodeFormat = (BMDTimecodeFormat) 0;

DeckLinkCaptureDelegate::DeckLinkCaptureDelegate ():priv (NULL), m_refCount (0)
{
  g_mutex_init (&m_mutex);
}

DeckLinkCaptureDelegate::~DeckLinkCaptureDelegate ()
{
  g_mutex_clear (&m_mutex);
}

ULONG DeckLinkCaptureDelegate::AddRef (void)
{
  g_mutex_lock (&m_mutex);
  m_refCount++;
  g_mutex_unlock (&m_mutex);

  return (ULONG) m_refCount;
}

ULONG DeckLinkCaptureDelegate::Release (void)
{
  g_mutex_lock (&m_mutex);
  m_refCount--;
  g_mutex_unlock (&m_mutex);

  if (m_refCount == 0) {
    delete
        this;
    return 0;
  }

  return (ULONG) m_refCount;
}

HRESULT
    DeckLinkCaptureDelegate::VideoInputFrameArrived (IDeckLinkVideoInputFrame *
    videoFrame, IDeckLinkAudioInputPacket * audioFrame)
{
  GstDecklinkSrc *decklinksrc;
  GstClock *clock;
  GstClockTime base_time, clock_time, capture_time;
  const char *timecodeString = NULL;

  g_return_val_if_fail (priv != NULL, S_OK);
  g_return_val_if_fail (GST_IS_DECKLINK_SRC (priv), S_OK);

  decklinksrc = GST_DECKLINK_SRC (priv);

  if (videoFrame == NULL) {
    GST_WARNING_OBJECT (decklinksrc, "video frame is NULL");
    return S_OK;
  }

  if (audioFrame == NULL) {
    GST_WARNING_OBJECT (decklinksrc, "audio frame is NULL");
    return S_OK;
  }

  if (videoFrame->GetFlags () & bmdFrameHasNoInputSource) {
    GST_DEBUG_OBJECT (decklinksrc, "Frame received - No input signal detected");
    return S_OK;
  }

  /* FIXME: g_timecodeFormat is inited to 0 and never changed? dead code? */
  if (g_timecodeFormat != 0) {
    IDeckLinkTimecode *timecode;
    if (videoFrame->GetTimecode (g_timecodeFormat, &timecode) == S_OK) {
      timecode->GetString (&timecodeString);
      CONVERT_COM_STRING (timecodeString);
    }
  }

  GST_OBJECT_LOCK (decklinksrc);
  if ((clock = GST_ELEMENT_CLOCK (decklinksrc))) {
    base_time = GST_ELEMENT (decklinksrc)->base_time;
    gst_object_ref (clock);
  } else {
    base_time = GST_CLOCK_TIME_NONE;
  }
  GST_OBJECT_UNLOCK (decklinksrc);
  if (clock) {
    clock_time = gst_clock_get_time (clock);
    gst_object_unref (clock);
  } else {
    clock_time = GST_CLOCK_TIME_NONE;
  }

  if (base_time != GST_CLOCK_TIME_NONE) {
    capture_time = clock_time - base_time;
  } else {
    capture_time = GST_CLOCK_TIME_NONE;
  }

  GST_DEBUG_OBJECT (decklinksrc,
      "Frame received [%s] - %s - %" GST_TIME_FORMAT "Size: %li bytes",
      timecodeString != NULL ? timecodeString : "No timecode", "Valid Frame",
      GST_TIME_ARGS (capture_time),
      videoFrame->GetRowBytes () * videoFrame->GetHeight ());

  if (timecodeString)
    FREE_COM_STRING (timecodeString);

  g_mutex_lock (&decklinksrc->mutex);
  if (decklinksrc->video_frame != NULL) {
    decklinksrc->dropped_frames++;
    decklinksrc->video_frame->Release ();
    if (decklinksrc->audio_frame) {
      decklinksrc->audio_frame->Release ();
    }
  }
  videoFrame->AddRef ();
  decklinksrc->video_frame = videoFrame;
  if (audioFrame) {
    audioFrame->AddRef ();
    decklinksrc->audio_frame = audioFrame;
  }
  decklinksrc->capture_time = capture_time;

  /* increment regardless whether frame was dropped or not */
  decklinksrc->frame_num++;

  g_cond_signal (&decklinksrc->cond);
  g_mutex_unlock (&decklinksrc->mutex);

  return S_OK;
}

HRESULT
    DeckLinkCaptureDelegate::VideoInputFormatChanged
    (BMDVideoInputFormatChangedEvents events, IDeckLinkDisplayMode * mode,
    BMDDetectedVideoInputFormatFlags) {
  GstDecklinkSrc *
      decklinksrc;

  g_return_val_if_fail (priv != NULL, S_OK);
  g_return_val_if_fail (GST_IS_DECKLINK_SRC (priv), S_OK);

  decklinksrc = GST_DECKLINK_SRC (priv);

  GST_ERROR_OBJECT (decklinksrc, "unimplemented: video input format changed");

  return S_OK;
}
