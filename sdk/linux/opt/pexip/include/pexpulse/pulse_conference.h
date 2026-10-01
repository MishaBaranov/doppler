/* Pexip Universal Library for Secure Engagement.
 *
 * Copyright (C) 2022 Pexip AS
 * @author Knut Saastad
 * @author Tulio Beloqui
 */

#ifndef _PULSE_CONFERENCE_H_
#define _PULSE_CONFERENCE_H_

#include "pulse_config.h"

PULSE_DECL_BEGIN

/**
 * @brief Conference participant role types.
 * Defines the privilege level of a participant in a conference.
 */
typedef enum _PulseConferenceRole
{
  PULSE_CONFERENCE_ROLE_HOST = 0, /* the participant has Host privileges. */
  PULSE_CONFERENCE_ROLE_GUEST,    /* the participant has Guest privileges. */
  PULSE_CONFERENCE_ROLE_UNKNOWN   /* the participant has Unknown privileges. [Teams participant] */
} PulseConferenceRole;

/**
 * @brief Conference signalling protocol types.
 * Defines the protocol used for a conference call.
 */
typedef enum _PulseConferenceProtocol
{
  PULSE_CONFERENCE_PROTOCOL_SIP = 0, /* Use protocol SIP */
  PULSE_CONFERENCE_PROTOCOL_H323,    /* Use protocol H323 */
  PULSE_CONFERENCE_PROTOCOL_RTMP,    /* Use protocol RTMP */
  PULSE_CONFERENCE_PROTOCOL_MSSIP,   /* for calls to Microsoft Skype for Business / Lync */
  PULSE_CONFERENCE_PROTOCOL_TEAMS,   /* for calls to Microsoft Skype for Business / Lync */
  PULSE_CONFERENCE_PROTOCOL_AUTO,    /* to use Call Routing Rules */
  PULSE_CONFERENCE_PROTOCOL_WEBRTC,  /* WebRTC call */
  PULSE_CONFERENCE_PROTOCOL_API,     /* API call */
  PULSE_CONFERENCE_PROTOCOL_NONE,    /* Empty response, when role == guest */
  PULSE_CONFERENCE_PROTOCOL_GMS      /* Google */
} PulseConferenceProtocol;

/**
 * @brief Conference call type.
 * The call type the conference is configured with server-side, on the Infinity management server, and reported back to
 * us in the request_token response. This is the prime limiter of what is possible media-wise in that conference: it
 * caps whatever the client asks for, so a client can negotiate less media than the call type allows, never more.
 *
 * It decides which media the call is permitted to carry:
 *   - PULSE_CONFERENCE_CALL_TYPE_VIDEO:      audio, main video and presentation.
 *   - PULSE_CONFERENCE_CALL_TYPE_VIDEO_ONLY: audio and main video. Presentation is not allowed.
 *   - PULSE_CONFERENCE_CALL_TYPE_AUDIO:      audio alone. Neither main video nor presentation is allowed.
 *
 * This is a different thing from a client-side 'audio-only' call, which is not signalled as such at all: a client
 * simply connects no main video input or output, and the resulting SDP then carries no main video media line. Such a
 * client keeps its presentation media line, as long as the conference call type allows presentation. A
 * presentation-only call is likewise a client-side construct, and needs a conference configured as
 * PULSE_CONFERENCE_CALL_TYPE_VIDEO.
 *
 * @see https://docs.pexip.com/api_client/api_rest.htm#request_token
 */
typedef enum _PulseConferenceCallType
{
  PULSE_CONFERENCE_CALL_TYPE_VIDEO = 0,  /* audio, main video and presentation. */
  PULSE_CONFERENCE_CALL_TYPE_VIDEO_ONLY, /* audio and main video, no presentation. */
  PULSE_CONFERENCE_CALL_TYPE_AUDIO,      /* audio alone, no main video and no presentation. */
  PULSE_CONFERENCE_CALL_TYPE_UNKNOWN     /* the call type is not known, e.g. before we are connected. */
} PulseConferenceCallType;

/**
 * @brief Conference keep-alive behaviour types.
 * Defines how a participant's presence affects conference lifetime.
 */
typedef enum _PulseConferenceKeepAliveType
{
  PULSE_CONFERENCE_KEEP_ALIVE =
    0, /* the conference continues to run until this participant disconnects (applies to Hosts only). */
  PULSE_CONFERENCE_KEEP_ALIVE_IF_MULTIPLE, /* the conference continues to run as long as there are two or more
                                              "keep_conference_alive_if_multiple" participants and at least one of them
                                              is a Host.*/
  PULSE_CONFERENCE_KEEP_ALIVE_NEVER        /* the conference terminates automatically if this is the only remaining
                                              participant.*/
} PulseConferenceKeepAliveType;

/**
 * @brief Conference call direction types.
 * Defines whether a call is inbound or outbound.
 */
typedef enum _PulseConferenceCallDirection
{
  PULSE_CONFERENCE_CALL_DIRECTION_IN = 0, /* Inbound call. */
  PULSE_CONFERENCE_CALL_DIRECTION_OUT     /* Outbound call. */
} PulseConferenceCallDirection;

/**
 * @brief Conference receive-presentation policy types.
 * Defines whether a participant is allowed to receive presentation content.
 */
typedef enum _PulseConferenceRxPresentationPolicy
{
  PULSE_CONFERENCE_PRESENTATION_POLICY_ALLOW = 0, /* Participant is administratively allowed to receive presentation. */
  PULSE_CONFERENCE_PRESENTATION_POLICY_DENY /* Participant is administratively disallowed to receive presentation. */
} PulseConferenceRxPresentationPolicy;

/**
 * @brief Conference service type.
 * Defines the type of service or call state a participant is in.
 */
typedef enum _PulseConferenceServiceType
{
  PULSE_CONFERENCE_SERVICE_TYPE_CONNECTING = 0, /* A dial-out participant that has not been answered. */
  PULSE_CONFERENCE_SERVICE_TYPE_WAITING_ROOM,   /* Participant is waiting to be allowed to join a locked conference. */
  PULSE_CONFERENCE_SERVICE_TYPE_IVR,            /* Participant is on the PIN entry screen. */
  PULSE_CONFERENCE_SERVICE_TYPE_CONFERENCE,     /* Participant is in a VMR. */
  PULSE_CONFERENCE_SERVICE_TYPE_LECTURE,        /* Participant is in a Virtual Auditorium. */
  PULSE_CONFERENCE_SERVICE_TYPE_GATEWAY,        /* Participant is in a gateway call. */
  PULSE_CONFERENCE_SERVICE_TYPE_TEST_CALL,      /* Participant is in a Test Call Service. */
  PULSE_CONFERENCE_SERVICE_TYPE_MEDIA_PLAYBACK  /* Participant is in a Media Playback call. */
} PulseConferenceServiceType;

PULSE_DECL_END

#endif