import {
  router,
  useLocalSearchParams,
} from "expo-router";
import {
  Grid3X3,
  Mic,
  MicOff,
  PhoneOff,
  Signal,
  Volume2,
} from "lucide-react-native";
import {
  useEffect,
  useState,
} from "react";
import {
  Pressable,
  StyleSheet,
  Text,
  View,
} from "react-native";
import {
  useSafeAreaInsets,
} from "react-native-safe-area-context";

import {
  colors,
  fonts,
  radius,
} from "../theme";

function formatDuration(
  seconds: number,
) {
  const minutes =
    Math.floor(
      seconds / 60,
    )
      .toString()
      .padStart(2, "0");

  const remainder =
    (seconds % 60)
      .toString()
      .padStart(2, "0");

  return `${minutes}:${remainder}`;
}

function Metric({
  label,
  value,
}: {
  label: string;
  value: string;
}) {
  return (
    <View style={styles.metric}>
      <Text style={styles.metricValue}>
        {value}
      </Text>

      <Text style={styles.metricLabel}>
        {label}
      </Text>
    </View>
  );
}

export default function ActiveCallScreen() {
  const insets =
    useSafeAreaInsets();

  const params =
    useLocalSearchParams<{
      number?: string;
    }>();

  const [
    elapsed,
    setElapsed,
  ] =
    useState(0);

  const [
    muted,
    setMuted,
  ] =
    useState(false);

  const [
    speaker,
    setSpeaker,
  ] =
    useState(false);

  useEffect(() => {
    const timer =
      setInterval(
        () =>
          setElapsed(
            (value) =>
              value + 1,
          ),
        1000,
      );

    return () =>
      clearInterval(timer);
  }, []);

  const number =
    String(
      params.number ||
      "0712 000 002",
    );

  return (
    <View
      style={[
        styles.root,
        {
          paddingTop:
            Math.max(
              insets.top + 10,
              24,
            ),
          paddingBottom:
            Math.max(
              insets.bottom + 18,
              26,
            ),
        },
      ]}
    >
      <View>
        <View style={styles.header}>
          <View style={styles.signalIcon}>
            <Signal
              size={20}
              color={colors.charcoal}
            />
          </View>

          <View style={{ flex: 1 }}>
            <Text style={styles.eyebrow}>
              CALL CONNECTED
            </Text>

            <Text style={styles.number}>
              {number}
            </Text>
          </View>

          <View style={styles.quality}>
            <Text style={styles.qualityText}>
              GOOD
            </Text>
          </View>
        </View>

        <Text style={styles.duration}>
          {formatDuration(
            elapsed,
          )}
        </Text>

        <View style={styles.metrics}>
          <Metric
            label="Signal"
            value="-62 dBm"
          />

          <Metric
            label="Latency"
            value="48 ms"
          />

          <Metric
            label="Jitter"
            value="6 ms"
          />

          <Metric
            label="Packet loss"
            value="0.5%"
          />
        </View>

        <View style={styles.notice}>
          <Text style={styles.noticeTitle}>
            Simulated media path
          </Text>

          <Text style={styles.noticeText}>
            These values are UI placeholders.
            The network simulator will provide
            live packet measurements next.
          </Text>
        </View>
      </View>

      <View>
        <View style={styles.controls}>
          <Pressable
            style={[
              styles.control,
              muted &&
                styles.controlActive,
            ]}
            onPress={() =>
              setMuted(
                (value) => !value,
              )
            }
          >
            {muted ? (
              <MicOff
                size={22}
                color={colors.white}
              />
            ) : (
              <Mic
                size={22}
                color={colors.charcoal}
              />
            )}

            <Text
              style={[
                styles.controlText,
                muted &&
                  styles.controlTextActive,
              ]}
            >
              Mute
            </Text>
          </Pressable>

          <Pressable
            style={styles.control}
          >
            <Grid3X3
              size={22}
              color={colors.charcoal}
            />

            <Text style={styles.controlText}>
              Keypad
            </Text>
          </Pressable>

          <Pressable
            style={[
              styles.control,
              speaker &&
                styles.controlActive,
            ]}
            onPress={() =>
              setSpeaker(
                (value) =>
                  !value,
              )
            }
          >
            <Volume2
              size={22}
              color={
                speaker
                  ? colors.white
                  : colors.charcoal
              }
            />

            <Text
              style={[
                styles.controlText,
                speaker &&
                  styles.controlTextActive,
              ]}
            >
              Speaker
            </Text>
          </Pressable>
        </View>

        <Pressable
          style={styles.end}
          onPress={() =>
            router.replace(
              "/(dialer-tabs)/recents" as any,
            )
          }
        >
          <PhoneOff
            size={24}
            color={colors.white}
          />

          <Text style={styles.endText}>
            End call
          </Text>
        </Pressable>
      </View>
    </View>
  );
}

const styles =
  StyleSheet.create({
    root: {
      flex: 1,
      paddingHorizontal: 18,
      backgroundColor:
        colors.canvas,
      justifyContent:
        "space-between",
    },
    header: {
      minHeight: 72,
      flexDirection: "row",
      alignItems: "center",
      gap: 11,
    },
    signalIcon: {
      width: 46,
      height: 46,
      borderRadius: 15,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
      alignItems: "center",
      justifyContent: "center",
    },
    eyebrow: {
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 8,
      letterSpacing: 1.2,
    },
    number: {
      marginTop: 3,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 20,
    },
    quality: {
      paddingHorizontal: 11,
      paddingVertical: 7,
      borderRadius: radius.pill,
      backgroundColor:
        colors.charcoal,
    },
    qualityText: {
      fontFamily: fonts.bold,
      color: colors.white,
      fontSize: 8,
      letterSpacing: 0.8,
    },
    duration: {
      marginTop: 42,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 42,
      textAlign: "center",
      letterSpacing: 1.5,
    },
    metrics: {
      marginTop: 30,
      flexDirection: "row",
      flexWrap: "wrap",
      gap: 8,
    },
    metric: {
      width: "48.5%",
      minHeight: 82,
      padding: 13,
      borderRadius: radius.large,
      borderWidth: 1,
      borderColor: colors.border,
      backgroundColor:
        colors.white,
      justifyContent: "center",
    },
    metricValue: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 18,
    },
    metricLabel: {
      marginTop: 4,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
    },
    notice: {
      marginTop: 12,
      padding: 14,
      borderRadius: radius.large,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
    },
    noticeTitle: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 10,
    },
    noticeText: {
      marginTop: 4,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
      lineHeight: 13,
    },
    controls: {
      marginBottom: 22,
      flexDirection: "row",
      justifyContent:
        "space-between",
    },
    control: {
      width: "31%",
      height: 84,
      borderRadius: radius.large,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
      alignItems: "center",
      justifyContent: "center",
      gap: 7,
    },
    controlActive: {
      backgroundColor:
        colors.charcoal,
      borderColor:
        colors.charcoal,
    },
    controlText: {
      fontFamily: fonts.bold,
      color: colors.muted,
      fontSize: 8,
    },
    controlTextActive: {
      color: colors.white,
    },
    end: {
      height: 58,
      borderRadius: radius.card,
      backgroundColor:
        colors.error,
      flexDirection: "row",
      alignItems: "center",
      justifyContent: "center",
      gap: 9,
    },
    endText: {
      fontFamily: fonts.bold,
      color: colors.white,
      fontSize: 11,
    },
  });
