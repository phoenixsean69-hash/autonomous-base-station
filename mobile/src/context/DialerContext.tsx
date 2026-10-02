import {
  router,
} from "expo-router";
import {
  Alert,
} from "react-native";
import {
  createContext,
  type PropsWithChildren,
  useCallback,
  useContext,
  useEffect,
  useMemo,
  useRef,
  useState,
} from "react";

import {
  getBtsUrl,
} from "../config/network";

export type Subscriber = {
  id: "A" | "B";
  name: string;
  number: string;
  initials: string;
};

export type ConnectionStatus =
  | "connecting"
  | "connected"
  | "offline";

export type NetworkMetrics = {
  packets_sent: number;
  packets_received: number;
  packets_dropped: number;
  latency_ms: number;
  jitter_ms: number;
  packet_loss_pct: number;
  rssi_dbm: number;
  traffic_load_pct: number;
  quality: string;
};

export type CallInfo = {
  call_id: string;
  caller: string;
  callee: string;
  state: string;
  duration_s: number;
  metrics: NetworkMetrics;
};

export type RecentCall = {
  id: string;
  direction:
    | "incoming"
    | "outgoing";
  number: string;
  detail: string;
  time: string;
};

export type SmsMessage = {
  sms_id: string;
  from: string;
  to: string;
  body: string;
  timestamp_ms: number;
  status: string;
  direction:
    | "incoming"
    | "outgoing";
};

export const SUBSCRIBERS: Subscriber[] = [
  {
    id: "A",
    name: "Subscriber A",
    number: "0712 000 001",
    initials: "A",
  },
  {
    id: "B",
    name: "Subscriber B",
    number: "0712 000 002",
    initials: "B",
  },
];

type DialerContextValue = {
  activeSubscriber: Subscriber | null;
  peerSubscriber: Subscriber | null;
  connectionStatus: ConnectionStatus;
  peerOnline: boolean;
  activeCall: CallInfo | null;
  metrics: NetworkMetrics | null;
  recentCalls: RecentCall[];
  messages: SmsMessage[];
  selectSubscriber:
    (subscriber: Subscriber) => void;
  resetSubscriber: () => void;
  startCall:
    (number: string) => boolean;
  sendSms:
    (
      number: string,
      body: string,
    ) => boolean;
  answerCall: () => void;
  rejectCall: () => void;
  endCall: () => void;
};

const DialerContext =
  createContext<DialerContextValue | null>(null);

function normalizeNumber(
  value: string,
) {
  return value.replace(
    /\D/g,
    "",
  );
}

export function formatPhoneNumber(
  value: string,
) {
  const digits =
    normalizeNumber(value);

  if (digits.length <= 4) {
    return digits;
  }

  if (digits.length <= 7) {
    return `${digits.slice(
      0,
      4,
    )} ${digits.slice(4)}`;
  }

  return `${digits.slice(
    0,
    4,
  )} ${digits.slice(
    4,
    7,
  )} ${digits.slice(
    7,
    10,
  )}`;
}

function friendlyFailure(
  reason: string,
) {
  switch (reason) {
    case "UNKNOWN_SUBSCRIBER":
      return "That number is not available.";
    case "CANNOT_CALL_SELF":
      return "You cannot call your own number.";
    case "CALLER_BUSY":
      return "You are already on a call.";
    case "CALLEE_BUSY":
      return "The other phone is busy.";
    case "CALLEE_OFFLINE":
    case "SUBSCRIBER_UNAVAILABLE":
      return "The other phone is currently unavailable.";
    case "BTS_RADIO_UNAVAILABLE":
      return "The BTS radio is currently unavailable.";
    case "INVALID_ANSWER":
      return "This call is no longer available.";
    default:
      return "The call could not be connected.";
  }
}

function durationText(
  seconds: number,
) {
  if (seconds < 60) {
    return `${seconds} sec`;
  }

  const minutes =
    Math.floor(
      seconds / 60,
    );

  const remaining =
    seconds % 60;

  if (!remaining) {
    return `${minutes} min`;
  }

  return `${minutes} min ${remaining} sec`;
}

export function DialerProvider({
  children,
}: PropsWithChildren) {
  const [
    activeSubscriber,
    setActiveSubscriber,
  ] =
    useState<Subscriber | null>(
      null,
    );

  const [
    connectionStatus,
    setConnectionStatus,
  ] =
    useState<ConnectionStatus>(
      "offline",
    );

  const [
    peerOnline,
    setPeerOnline,
  ] =
    useState(false);

  const [
    activeCall,
    setActiveCall,
  ] =
    useState<CallInfo | null>(
      null,
    );

  const [
    metrics,
    setMetrics,
  ] =
    useState<NetworkMetrics | null>(
      null,
    );

  const [
    recentCalls,
    setRecentCalls,
  ] =
    useState<RecentCall[]>(
      [],
    );

  const [
    messages,
    setMessages,
  ] =
    useState<SmsMessage[]>(
      [],
    );

  const socketRef =
    useRef<WebSocket | null>(
      null,
    );

  const activeSubscriberRef =
    useRef<Subscriber | null>(
      null,
    );

  useEffect(() => {
    activeSubscriberRef.current =
      activeSubscriber;
  }, [activeSubscriber]);

  const peerSubscriber =
    useMemo(() => {
      if (!activeSubscriber) {
        return null;
      }

      return (
        SUBSCRIBERS.find(
          (subscriber) =>
            subscriber.id !==
            activeSubscriber.id,
        ) ?? null
      );
    }, [activeSubscriber]);

  const upsertMessage =
    useCallback(
      (next: SmsMessage) => {
        setMessages(
          (current) => {
            const index =
              current.findIndex(
                (item) =>
                  item.sms_id ===
                  next.sms_id,
              );

            if (index < 0) {
              return [
                ...current,
                next,
              ].slice(-100);
            }

            const updated = [
              ...current,
            ];

            updated[index] = {
              ...updated[index],
              ...next,
            };

            return updated;
          },
        );
      },
      [],
    );

  const addRecentCall =
    useCallback(
      (
        call: CallInfo,
        ending:
          | "ended"
          | "rejected"
          | "network",
      ) => {
        const own =
          normalizeNumber(
            activeSubscriberRef
              .current?.number ??
              "",
          );

        if (!own) {
          return;
        }

        const incoming =
          call.callee === own;

        const peer =
          incoming
            ? call.caller
            : call.callee;

        let detail = incoming
          ? "Incoming call"
          : "Outgoing call";

        if (
          ending === "rejected"
        ) {
          detail = incoming
            ? "Declined"
            : "Call declined";
        }
        else if (
          ending === "network"
        ) {
          detail =
            "Call ended • connection lost";
        }
        else if (
          call.duration_s > 0
        ) {
          detail =
            `${detail} • ${durationText(
              call.duration_s,
            )}`;
        }

        const entry: RecentCall = {
          id:
            `${call.call_id}-${Date.now()}`,
          direction:
            incoming
              ? "incoming"
              : "outgoing",
          number:
            formatPhoneNumber(
              peer,
            ),
          detail,
          time: "Just now",
        };

        setRecentCalls(
          (current) => [
            entry,
            ...current,
          ].slice(
            0,
            20,
          ),
        );
      },
      [],
    );

  useEffect(() => {
    if (!activeSubscriber) {
      socketRef.current?.close();
      socketRef.current = null;
      setConnectionStatus(
        "offline",
      );
      setPeerOnline(false);
      setActiveCall(null);
      setMetrics(null);
      return;
    }

    let disposed = false;
    let reconnectTimer:
      ReturnType<
        typeof setTimeout
      >
      | null = null;

    const connect = () => {
      if (disposed) {
        return;
      }

      setConnectionStatus(
        "connecting",
      );

      const base =
        getBtsUrl();

      const number =
        normalizeNumber(
          activeSubscriber.number,
        );

      const socket =
        new WebSocket(
          `${base}/ws/${number}`,
        );

      socketRef.current =
        socket;

      socket.onopen = () => {
        if (!disposed) {
          setConnectionStatus(
            "connecting",
          );
        }
      };

      socket.onmessage = (
        event,
      ) => {
        if (disposed) {
          return;
        }

        let message: any;

        try {
          message =
            JSON.parse(
              String(
                event.data,
              ),
            );
        }
        catch {
          return;
        }

        const type =
          String(
            message?.type ??
            "",
          );

        if (
          type ===
          "registered"
        ) {
          setConnectionStatus(
            "connected",
          );
          return;
        }

        if (
          type ===
          "subscribers.status"
        ) {
          const peerNumber =
            normalizeNumber(
              SUBSCRIBERS.find(
                (subscriber) =>
                  subscriber.id !==
                  activeSubscriber.id,
              )?.number ??
              "",
            );

          const status =
            Array.isArray(
              message.subscribers,
            )
              ? message
                  .subscribers
                  .find(
                    (
                      subscriber:
                        any,
                    ) =>
                      normalizeNumber(
                        String(
                          subscriber
                            ?.number ??
                            "",
                        ),
                      ) ===
                      peerNumber,
                  )
              : null;

          setPeerOnline(
            Boolean(
              status?.online,
            ),
          );

          return;
        }

        if (
          type ===
            "sms.incoming" ||
          type ===
            "sms.status"
        ) {
          const smsId =
            String(
              message.sms_id ??
                "",
            );

          const from =
            normalizeNumber(
              String(
                message.from ??
                  "",
              ),
            );

          const to =
            normalizeNumber(
              String(
                message.to ??
                  "",
              ),
            );

          const body =
            String(
              message.body ??
                "",
            );

          if (
            !smsId ||
            !from ||
            !to ||
            !body
          ) {
            return;
          }

          const own =
            normalizeNumber(
              activeSubscriber.number,
            );

          upsertMessage({
            sms_id: smsId,
            from,
            to,
            body,
            timestamp_ms:
              Number(
                message.timestamp_ms ??
                  Date.now(),
              ),
            status:
              String(
                message.status ??
                  "DELIVERED",
              ),
            direction:
              from === own
                ? "outgoing"
                : "incoming",
          });

          return;
        }

        if (
          type ===
          "sms.failed"
        ) {
          Alert.alert(
            "Message not sent",
            "The BTS could not send this message.",
          );
          return;
        }

        if (
          type ===
          "call.ringing"
        ) {
          const call =
            message.call as
              CallInfo;

          setActiveCall(call);
          setMetrics(
            call.metrics,
          );

          router.push({
            pathname:
              "/outgoing-call" as any,
            params: {
              number:
                formatPhoneNumber(
                  call.callee,
                ),
            },
          });

          return;
        }

        if (
          type ===
          "call.incoming"
        ) {
          const call =
            message.call as
              CallInfo;

          setActiveCall(call);
          setMetrics(
            call.metrics,
          );

          router.push({
            pathname:
              "/incoming-call" as any,
            params: {
              from:
                formatPhoneNumber(
                  call.caller,
                ),
            },
          });

          return;
        }

        if (
          type ===
          "call.connected"
        ) {
          const call =
            message.call as
              CallInfo;

          setActiveCall(call);
          setMetrics(
            call.metrics,
          );

          const own =
            normalizeNumber(
              activeSubscriber
                .number,
            );

          const peer =
            call.caller === own
              ? call.callee
              : call.caller;

          router.replace({
            pathname:
              "/active-call" as any,
            params: {
              number:
                formatPhoneNumber(
                  peer,
                ),
            },
          });

          return;
        }

        if (
          type ===
          "call.metrics"
        ) {
          const call =
            message.call as
              CallInfo;

          setActiveCall(call);
          setMetrics(
            call.metrics,
          );
          return;
        }

        if (
          type ===
          "call.rejected"
        ) {
          const call =
            message.call as
              CallInfo;

          addRecentCall(
            call,
            "rejected",
          );

          setActiveCall(null);
          setMetrics(null);

          Alert.alert(
            "Call declined",
            "The other phone declined the call.",
          );

          router.replace(
            "/(dialer-tabs)/recents" as any,
          );

          return;
        }

        if (
          type ===
          "call.ended"
        ) {
          const call =
            message.call as
              CallInfo;

          const reason =
            String(
              message.reason ??
              "",
            );

          addRecentCall(
            call,
            reason ===
              "NETWORK_FAILURE"
              ? "network"
              : "ended",
          );

          setActiveCall(null);
          setMetrics(null);

          if (
            reason ===
            "NETWORK_FAILURE"
          ) {
            Alert.alert(
              "Call ended",
              "The mobile connection was lost.",
            );
          }

          router.replace(
            "/(dialer-tabs)/recents" as any,
          );

          return;
        }

        if (
          type ===
          "call.failed"
        ) {
          Alert.alert(
            "Call could not connect",
            friendlyFailure(
              String(
                message.reason ??
                "",
              ),
            ),
          );

          setActiveCall(null);
          setMetrics(null);

          router.replace(
            "/(dialer-tabs)" as any,
          );
        }
      };

      socket.onerror = () => {
        if (!disposed) {
          setConnectionStatus(
            "offline",
          );
        }
      };

      socket.onclose = (
        event,
      ) => {
        if (
          socketRef.current ===
          socket
        ) {
          socketRef.current =
            null;
        }

        if (disposed) {
          return;
        }

        setConnectionStatus(
          "offline",
        );
        setPeerOnline(false);

        if (
          event.code === 1012
        ) {
          Alert.alert(
            "Phone moved",
            "This number is now active on another device.",
          );
          return;
        }

        reconnectTimer =
          setTimeout(
            connect,
            2000,
          );
      };
    };

    connect();

    return () => {
      disposed = true;

      if (reconnectTimer) {
        clearTimeout(
          reconnectTimer,
        );
      }

      const socket =
        socketRef.current;

      socketRef.current =
        null;

      if (
        socket &&
        (
          socket.readyState ===
            WebSocket.OPEN ||
          socket.readyState ===
            WebSocket.CONNECTING
        )
      ) {
        socket.close();
      }
    };
  }, [
    activeSubscriber,
    addRecentCall,
    upsertMessage,
  ]);

  const send =
    useCallback(
      (payload: object) => {
        const socket =
          socketRef.current;

        if (
          !socket ||
          socket.readyState !==
            WebSocket.OPEN
        ) {
          Alert.alert(
            "No connection",
            "The mobile network is unavailable right now. Please try again.",
          );

          return false;
        }

        socket.send(
          JSON.stringify(
            payload,
          ),
        );

        return true;
      },
      [],
    );

  const startCall =
    useCallback(
      (number: string) => {
        const normalized =
          normalizeNumber(
            number,
          );

        if (!normalized) {
          return false;
        }

        return send({
          type: "call.start",
          to: normalized,
        });
      },
      [send],
    );

  const sendSms =
    useCallback(
      (
        number: string,
        body: string,
      ) => {
        const normalized =
          normalizeNumber(
            number,
          );

        const trimmed =
          body.trim();

        if (
          !normalized ||
          !trimmed
        ) {
          return false;
        }

        return send({
          type: "sms.send",
          to: normalized,
          body: trimmed,
        });
      },
      [send],
    );

  const answerCall =
    useCallback(() => {
      if (!activeCall) {
        return;
      }

      send({
        type: "call.answer",
        call_id:
          activeCall.call_id,
      });
    }, [
      activeCall,
      send,
    ]);

  const rejectCall =
    useCallback(() => {
      if (!activeCall) {
        router.replace(
          "/(dialer-tabs)" as any,
        );
        return;
      }

      send({
        type: "call.reject",
        call_id:
          activeCall.call_id,
      });
    }, [
      activeCall,
      send,
    ]);

  const endCall =
    useCallback(() => {
      if (!activeCall) {
        router.replace(
          "/(dialer-tabs)" as any,
        );
        return;
      }

      const sent =
        send({
          type: "call.end",
          call_id:
            activeCall.call_id,
        });

      if (!sent) {
        setActiveCall(null);
        setMetrics(null);

        router.replace(
          "/(dialer-tabs)/recents" as any,
        );
      }
    }, [
      activeCall,
      send,
    ]);

  const resetSubscriber =
    useCallback(() => {
      socketRef.current?.close();
      socketRef.current = null;
      setActiveCall(null);
      setMetrics(null);
      setPeerOnline(false);
      setMessages([]);
      setActiveSubscriber(null);
    }, []);

  const value =
    useMemo(
      () => ({
        activeSubscriber,
        peerSubscriber,
        connectionStatus,
        peerOnline,
        activeCall,
        metrics,
        recentCalls,
        messages,
        selectSubscriber:
          (
            subscriber:
              Subscriber,
          ) =>
            setActiveSubscriber(
              subscriber,
            ),
        resetSubscriber,
        startCall,
        sendSms,
        answerCall,
        rejectCall,
        endCall,
      }),
      [
        activeSubscriber,
        peerSubscriber,
        connectionStatus,
        peerOnline,
        activeCall,
        metrics,
        recentCalls,
        messages,
        resetSubscriber,
        startCall,
        sendSms,
        answerCall,
        rejectCall,
        endCall,
      ],
    );

  return (
    <DialerContext.Provider
      value={value}
    >
      {children}
    </DialerContext.Provider>
  );
}

export function useDialer() {
  const value =
    useContext(
      DialerContext,
    );

  if (!value) {
    throw new Error(
      "useDialer must be used inside DialerProvider",
    );
  }

  return value;
}
