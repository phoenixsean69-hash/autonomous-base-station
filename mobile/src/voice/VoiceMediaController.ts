// ABS WEBRTC VOICE ALL SIX V4
import {
  mediaDevices,
  RTCPeerConnection,
  RTCIceCandidate,
  RTCSessionDescription,
  type MediaStream,
} from "react-native-webrtc";

export type VoiceMediaState =
  | "idle"
  | "starting"
  | "connected"
  | "failed";

export type VoiceCall = {
  call_id: string;
  caller: string;
  callee: string;
};

type SignalSender =
  (payload: Record<string, unknown>) => boolean;

type VoiceCallbacks = {
  onState: (state: VoiceMediaState) => void;
  onMuted: (muted: boolean) => void;
  onError: (message: string) => void;
};

const DEMO_SUBSCRIBERS = new Set([
  "0712000001",
  "0712000002",
  "0712000003",
  "0712000004",
  "0712000005",
  "0712000006",
]);

function normalizeNumber(value: string) {
  return String(value ?? "").replace(/\D/g, "");
}

function isSupportedCall(call: VoiceCall) {
  const caller = normalizeNumber(call.caller);
  const callee = normalizeNumber(call.callee);

  return (
    DEMO_SUBSCRIBERS.has(caller) &&
    DEMO_SUBSCRIBERS.has(callee) &&
    caller !== callee
  );
}

function stopStream(stream: MediaStream | null) {
  if (!stream) {
    return;
  }

  for (const track of stream.getTracks()) {
    try {
      track.enabled = false;
      track.stop();
    }
    catch {
      // Best-effort cleanup.
    }
  }
}

export class VoiceMediaController {
  private peer: RTCPeerConnection | null = null;
  private localStream: MediaStream | null = null;
  private callId: string | null = null;
  private starting:
    Promise<RTCPeerConnection | null> | null = null;
  private pendingIce: RTCIceCandidate[] = [];
  private offerSentFor: string | null = null;
  private muted = false;

  constructor(
    private readonly callbacks: VoiceCallbacks,
  ) {}

  supports(call: VoiceCall) {
    return isSupportedCall(call);
  }

  stop(nextState: VoiceMediaState = "idle") {
    const peer = this.peer;
    const stream = this.localStream;

    this.peer = null;
    this.localStream = null;
    this.callId = null;
    this.starting = null;
    this.pendingIce = [];
    this.offerSentFor = null;
    this.muted = false;

    try {
      peer?.close();
    }
    catch {
      // Best-effort cleanup.
    }

    stopStream(stream);

    this.callbacks.onMuted(false);
    this.callbacks.onState(nextState);
  }

  toggleMute() {
    const stream = this.localStream;

    if (!stream) {
      return;
    }

    this.muted = !this.muted;

    for (const track of stream.getAudioTracks()) {
      track.enabled = !this.muted;
    }

    this.callbacks.onMuted(this.muted);
  }

  private sendSignal(
    send: SignalSender,
    payload: Record<string, unknown>,
  ) {
    const sent = send(payload);

    if (!sent) {
      throw new Error("BTS WebSocket is not open.");
    }
  }

  private async flushPendingIce(
    peer: RTCPeerConnection,
  ) {
    if (!peer.remoteDescription) {
      return;
    }

    const queued = this.pendingIce;
    this.pendingIce = [];

    for (const candidate of queued) {
      await peer.addIceCandidate(candidate);
    }
  }

  private async ensurePeer(
    call: VoiceCall,
    send: SignalSender,
  ): Promise<RTCPeerConnection | null> {
    if (!this.supports(call)) {
      return null;
    }

    if (
      this.callId === call.call_id &&
      this.peer
    ) {
      return this.peer;
    }

    if (
      this.callId === call.call_id &&
      this.starting
    ) {
      return this.starting;
    }

    if (
      this.callId &&
      this.callId !== call.call_id
    ) {
      this.stop();
    }

    this.callId = call.call_id;
    this.callbacks.onState("starting");

    const starting =
      (async () => {
        try {
          const stream =
            await mediaDevices.getUserMedia({
              audio: true,
              video: false,
            });

          if (this.callId !== call.call_id) {
            stopStream(stream);
            return null;
          }

          const peer =
            new RTCPeerConnection({
              // V1 is a same-LAN lab call.
              // TURN/STUN comes later.
              iceServers: [],
            });

          this.localStream = stream;
          this.peer = peer;

          for (const track of stream.getTracks()) {
            peer.addTrack(track, stream);
          }

          peer.onicecandidate = (event: any) => {
            if (
              !event.candidate ||
              this.callId !== call.call_id
            ) {
              return;
            }

            try {
              this.sendSignal(
                send,
                {
                  type: "webrtc.ice",
                  call_id: call.call_id,
                  candidate:
                    event.candidate.toJSON(),
                },
              );
            }
            catch (error) {
              console.warn(
                "[VOICE] ICE relay failed",
                error,
              );
            }
          };

          peer.ontrack = (event: any) => {
            if (
              this.callId === call.call_id &&
              event.track?.kind === "audio"
            ) {
              this.callbacks.onState(
                "connected",
              );
            }
          };

          peer.onconnectionstatechange = () => {
            if (this.callId !== call.call_id) {
              return;
            }

            if (
              peer.connectionState ===
              "connected"
            ) {
              this.callbacks.onState(
                "connected",
              );
            }
            else if (
              peer.connectionState ===
              "failed"
            ) {
              this.stop("failed");
              this.callbacks.onError(
                "The WebRTC media connection failed.",
              );
            }
          };

          return peer;
        }
        catch (error) {
          console.warn(
            "[VOICE] microphone/peer setup failed",
            error,
          );

          if (this.callId === call.call_id) {
            this.stop("failed");
            this.callbacks.onError(
              "Microphone access or WebRTC setup failed.",
            );
          }

          return null;
        }
      })();

    this.starting = starting;

    try {
      return await starting;
    }
    finally {
      if (this.starting === starting) {
        this.starting = null;
      }
    }
  }

  async start(
    call: VoiceCall,
    ownNumber: string,
    send: SignalSender,
  ) {
    if (!this.supports(call)) {
      return;
    }

    const peer =
      await this.ensurePeer(
        call,
        send,
      );

    if (
      !peer ||
      this.callId !== call.call_id
    ) {
      return;
    }

    const own =
      normalizeNumber(ownNumber);

    const caller =
      normalizeNumber(call.caller);

    if (caller !== own) {
      // Callee waits for caller's offer.
      return;
    }

    if (
      this.offerSentFor === call.call_id
    ) {
      return;
    }

    this.offerSentFor =
      call.call_id;

    try {
      const offer =
        await peer.createOffer();

      await peer.setLocalDescription(
        offer,
      );

      const local =
        peer.localDescription ?? offer;

      this.sendSignal(
        send,
        {
          type: "webrtc.offer",
          call_id: call.call_id,
          sdp: {
            type: local.type,
            sdp: local.sdp,
          },
        },
      );
    }
    catch (error) {
      console.warn(
        "[VOICE] offer failed",
        error,
      );
      this.stop("failed");
      this.callbacks.onError(
        "Could not start the voice media session.",
      );
    }
  }

  async handleSignal(
    message: any,
    call: VoiceCall | null,
    ownNumber: string,
    send: SignalSender,
  ) {
    if (
      !call ||
      !this.supports(call) ||
      String(message?.call_id ?? "") !==
        call.call_id
    ) {
      return;
    }

    const type =
      String(message?.type ?? "");

    if (
      type !== "webrtc.offer" &&
      type !== "webrtc.answer" &&
      type !== "webrtc.ice"
    ) {
      return;
    }

    const peer =
      await this.ensurePeer(
        call,
        send,
      );

    if (!peer) {
      return;
    }

    const own =
      normalizeNumber(ownNumber);

    const caller =
      normalizeNumber(call.caller);

    const callee =
      normalizeNumber(call.callee);

    try {
      if (type === "webrtc.ice") {
        const rawCandidate =
          message?.candidate;

        if (!rawCandidate) {
          return;
        }

        const candidate =
          new RTCIceCandidate(
            rawCandidate,
          );

        if (!peer.remoteDescription) {
          this.pendingIce.push(
            candidate,
          );
          return;
        }

        await peer.addIceCandidate(
          candidate,
        );
        return;
      }

      if (type === "webrtc.offer") {
        if (own !== callee) {
          return;
        }

        const rawSdp =
          message?.sdp;

        if (!rawSdp) {
          throw new Error(
            "Offer has no SDP.",
          );
        }

        await peer.setRemoteDescription(
          new RTCSessionDescription(
            rawSdp,
          ),
        );

        await this.flushPendingIce(
          peer,
        );

        const answer =
          await peer.createAnswer();

        await peer.setLocalDescription(
          answer,
        );

        const local =
          peer.localDescription ??
          answer;

        this.sendSignal(
          send,
          {
            type: "webrtc.answer",
            call_id: call.call_id,
            sdp: {
              type: local.type,
              sdp: local.sdp,
            },
          },
        );

        return;
      }

      if (type === "webrtc.answer") {
        if (own !== caller) {
          return;
        }

        const rawSdp =
          message?.sdp;

        if (!rawSdp) {
          throw new Error(
            "Answer has no SDP.",
          );
        }

        await peer.setRemoteDescription(
          new RTCSessionDescription(
            rawSdp,
          ),
        );

        await this.flushPendingIce(
          peer,
        );
      }
    }
    catch (error) {
      console.warn(
        "[VOICE] signalling failed",
        error,
      );

      this.stop("failed");
      this.callbacks.onError(
        "The voice media negotiation failed.",
      );
    }
  }
}
