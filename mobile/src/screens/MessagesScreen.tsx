import {
  MessageSquareText,
  SendHorizontal,
} from "lucide-react-native";
import {
  KeyboardAvoidingView,
  Platform,
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  TextInput,
  View,
} from "react-native";
import {
  useEffect,
  useRef,
  useState,
} from "react";
import {
  useSafeAreaInsets,
} from "react-native-safe-area-context";

import {
  useDialer,
} from "../context/DialerContext";
import {
  colors,
  fonts,
  radius,
} from "../theme";

function normalize(
  value: string,
) {
  return value.replace(
    /\D/g,
    "",
  );
}

function messageTime(
  timestampMs: number,
) {
  try {
    return new Date(
      timestampMs,
    ).toLocaleTimeString(
      [],
      {
        hour: "2-digit",
        minute: "2-digit",
      },
    );
  }
  catch {
    return "";
  }
}

function statusText(
  value: string,
) {
  const normalized =
    value.toUpperCase();

  if (
    normalized ===
    "DELIVERED"
  ) {
    return "Delivered";
  }

  if (
    normalized ===
    "QUEUED"
  ) {
    return "Queued";
  }

  if (
    normalized ===
    "FAILED"
  ) {
    return "Failed";
  }

  return value;
}

export default function MessagesScreen() {
  const insets =
    useSafeAreaInsets();

  const {
    activeSubscriber,
    peerSubscriber,
    peerOnline,
    connectionStatus,
    messages,
    sendSms,
  } = useDialer();

  const [
    draft,
    setDraft,
  ] =
    useState("");

  const scrollRef =
    useRef<ScrollView | null>(
      null,
    );

  const own =
    normalize(
      activeSubscriber
        ?.number ??
        "",
    );

  const peer =
    normalize(
      peerSubscriber
        ?.number ??
        "",
    );

  const conversation =
    messages.filter(
      (message) =>
        (
          message.from === own &&
          message.to === peer
        ) ||
        (
          message.from === peer &&
          message.to === own
        ),
    );

  useEffect(() => {
    const timer =
      setTimeout(
        () =>
          scrollRef.current
            ?.scrollToEnd({
              animated: true,
            }),
        50,
      );

    return () =>
      clearTimeout(timer);
  }, [
    conversation.length,
  ]);

  const canSend =
    connectionStatus ===
      "connected" &&
    Boolean(
      peerSubscriber,
    ) &&
    Boolean(
      draft.trim(),
    );

  const submit = () => {
    if (
      !canSend ||
      !peerSubscriber
    ) {
      return;
    }

    const sent =
      sendSms(
        peerSubscriber.number,
        draft,
      );

    if (sent) {
      setDraft("");
    }
  };

  return (
    <KeyboardAvoidingView
      style={styles.root}
      behavior={
        Platform.OS === "ios"
          ? "padding"
          : undefined
      }
    >
      <View
        style={[
          styles.content,
          {
            paddingTop:
              Math.max(
                insets.top + 12,
                24,
              ),
          },
        ]}
      >
        <Text style={styles.eyebrow}>
          {activeSubscriber?.number}
        </Text>

        <Text style={styles.title}>
          Messages
        </Text>

        <View style={styles.peerCard}>
          <View style={styles.peerIcon}>
            <MessageSquareText
              size={21}
              color={colors.charcoal}
            />
          </View>

          <View style={styles.peerText}>
            <Text style={styles.peerName}>
              {peerSubscriber?.name ??
                "Other phone"}
            </Text>

            <Text style={styles.peerNumber}>
              {peerSubscriber?.number ??
                "No recipient"}
            </Text>
          </View>

          <Text style={styles.peerStatus}>
            {peerOnline
              ? "Available"
              : "Offline"}
          </Text>
        </View>

        <ScrollView
          ref={scrollRef}
          style={styles.thread}
          contentContainerStyle={
            conversation.length
              ? styles.threadContent
              : styles.emptyThread
          }
          keyboardShouldPersistTaps="handled"
        >
          {conversation.length ? (
            conversation.map(
              (message) => {
                const outgoing =
                  message.direction ===
                  "outgoing";

                return (
                  <View
                    key={message.sms_id}
                    style={[
                      styles.messageRow,
                      outgoing
                        ? styles.outgoingRow
                        : styles.incomingRow,
                    ]}
                  >
                    <View
                      style={[
                        styles.bubble,
                        outgoing
                          ? styles.outgoingBubble
                          : styles.incomingBubble,
                      ]}
                    >
                      <Text
                        style={[
                          styles.body,
                          outgoing &&
                            styles.outgoingBody,
                        ]}
                      >
                        {message.body}
                      </Text>

                      <View
                        style={styles.metaRow}
                      >
                        <Text
                          style={[
                            styles.meta,
                            outgoing &&
                              styles.outgoingMeta,
                          ]}
                        >
                          {messageTime(
                            message.timestamp_ms,
                          )}
                        </Text>

                        {outgoing ? (
                          <Text
                            style={[
                              styles.meta,
                              styles.outgoingMeta,
                            ]}
                          >
                            {statusText(
                              message.status,
                            )}
                          </Text>
                        ) : null}
                      </View>
                    </View>
                  </View>
                );
              },
            )
          ) : (
            <View style={styles.empty}>
              <MessageSquareText
                size={26}
                color={colors.softMuted}
              />

              <Text style={styles.emptyTitle}>
                No messages yet
              </Text>

              <Text style={styles.emptyText}>
                Send an SMS through BTS-001 to start this conversation.
              </Text>
            </View>
          )}
        </ScrollView>

        <View style={styles.composer}>
          <TextInput
            value={draft}
            onChangeText={setDraft}
            placeholder={
              connectionStatus ===
              "connected"
                ? "Type a message"
                : "Waiting for BTS connection"
            }
            placeholderTextColor={
              colors.softMuted
            }
            multiline
            maxLength={160}
            style={styles.input}
            editable={
              connectionStatus ===
              "connected"
            }
          />

          <Pressable
            onPress={submit}
            disabled={!canSend}
            style={[
              styles.sendButton,
              !canSend &&
                styles.sendButtonDisabled,
            ]}
          >
            <SendHorizontal
              size={20}
              color={colors.white}
            />
          </Pressable>
        </View>
      </View>
    </KeyboardAvoidingView>
  );
}

const styles =
  StyleSheet.create({
    root: {
      flex: 1,
      backgroundColor:
        colors.canvas,
    },
    content: {
      flex: 1,
      paddingHorizontal: 18,
      paddingBottom: 12,
    },
    eyebrow: {
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 8,
      letterSpacing: 1.2,
    },
    title: {
      marginTop: 5,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 25,
    },
    peerCard: {
      marginTop: 16,
      padding: 13,
      borderWidth: 1,
      borderColor: colors.border,
      borderRadius: radius.large,
      backgroundColor:
        colors.surfaceSoft,
      flexDirection: "row",
      alignItems: "center",
      gap: 11,
    },
    peerIcon: {
      width: 42,
      height: 42,
      borderRadius: 13,
      backgroundColor:
        colors.white,
      alignItems: "center",
      justifyContent: "center",
    },
    peerText: {
      flex: 1,
    },
    peerName: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 11,
    },
    peerNumber: {
      marginTop: 2,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
    },
    peerStatus: {
      fontFamily: fonts.bold,
      color: colors.charcoalSoft,
      fontSize: 8,
    },
    thread: {
      flex: 1,
      marginTop: 14,
    },
    threadContent: {
      flexGrow: 1,
      paddingVertical: 8,
      gap: 8,
    },
    emptyThread: {
      flexGrow: 1,
      alignItems: "center",
      justifyContent: "center",
      paddingHorizontal: 28,
    },
    empty: {
      alignItems: "center",
    },
    emptyTitle: {
      marginTop: 9,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 12,
    },
    emptyText: {
      marginTop: 5,
      maxWidth: 240,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 9,
      lineHeight: 14,
      textAlign: "center",
    },
    messageRow: {
      width: "100%",
      flexDirection: "row",
    },
    incomingRow: {
      justifyContent:
        "flex-start",
    },
    outgoingRow: {
      justifyContent:
        "flex-end",
    },
    bubble: {
      maxWidth: "82%",
      paddingHorizontal: 13,
      paddingVertical: 10,
      borderRadius: 17,
    },
    incomingBubble: {
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
    },
    outgoingBubble: {
      backgroundColor:
        colors.charcoal,
    },
    body: {
      fontFamily: fonts.regular,
      color: colors.text,
      fontSize: 11,
      lineHeight: 16,
    },
    outgoingBody: {
      color: colors.white,
    },
    metaRow: {
      marginTop: 5,
      flexDirection: "row",
      justifyContent:
        "space-between",
      gap: 10,
    },
    meta: {
      fontFamily: fonts.regular,
      color: colors.softMuted,
      fontSize: 7,
    },
    outgoingMeta: {
      color: "#D0D0D0",
    },
    composer: {
      paddingTop: 10,
      borderTopWidth: 1,
      borderTopColor:
        colors.border,
      flexDirection: "row",
      alignItems: "flex-end",
      gap: 9,
    },
    input: {
      flex: 1,
      minHeight: 46,
      maxHeight: 110,
      paddingHorizontal: 14,
      paddingVertical: 11,
      borderWidth: 1,
      borderColor: colors.border,
      borderRadius: 16,
      backgroundColor:
        colors.surfaceSoft,
      fontFamily: fonts.regular,
      color: colors.text,
      fontSize: 11,
    },
    sendButton: {
      width: 46,
      height: 46,
      borderRadius: 15,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    sendButtonDisabled: {
      opacity: 0.35,
    },
  });
