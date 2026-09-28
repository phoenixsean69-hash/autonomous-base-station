import {
  PhoneIncoming,
  PhoneMissed,
  PhoneOutgoing,
} from "lucide-react-native";
import {
  ScrollView,
  StyleSheet,
  Text,
  View,
} from "react-native";
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

const CALLS = [
  {
    type: "outgoing",
    number: "0712 000 002",
    detail:
      "Outgoing • 2 min 14 sec",
    time: "Just now",
  },
  {
    type: "incoming",
    number: "0712 000 002",
    detail:
      "Incoming • 54 sec",
    time: "Today",
  },
  {
    type: "missed",
    number: "0712 000 002",
    detail: "Missed call",
    time: "Yesterday",
  },
] as const;

function CallIcon({
  type,
}: {
  type:
    | "outgoing"
    | "incoming"
    | "missed";
}) {
  if (type === "incoming") {
    return (
      <PhoneIncoming
        size={19}
        color={colors.charcoal}
      />
    );
  }

  if (type === "missed") {
    return (
      <PhoneMissed
        size={19}
        color={colors.error}
      />
    );
  }

  return (
    <PhoneOutgoing
      size={19}
      color={colors.charcoal}
    />
  );
}

export default function RecentsScreen() {
  const insets =
    useSafeAreaInsets();

  const {
    activeSubscriber,
  } = useDialer();

  return (
    <ScrollView
      style={styles.root}
      contentContainerStyle={[
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
        Recent calls
      </Text>

      <Text style={styles.subtitle}>
        Mock history for the UI phase.
      </Text>

      <View style={styles.list}>
        {CALLS.map(
          (call, index) => (
            <View
              key={`${call.time}-${index}`}
              style={styles.row}
            >
              <View style={styles.icon}>
                <CallIcon
                  type={call.type}
                />
              </View>

              <View style={{ flex: 1 }}>
                <Text style={styles.number}>
                  {call.number}
                </Text>

                <Text
                  style={[
                    styles.detail,
                    call.type ===
                      "missed" &&
                      styles.missed,
                  ]}
                >
                  {call.detail}
                </Text>
              </View>

              <Text style={styles.time}>
                {call.time}
              </Text>
            </View>
          ),
        )}
      </View>
    </ScrollView>
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
      paddingHorizontal: 18,
      paddingBottom: 34,
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
    subtitle: {
      marginTop: 6,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 9,
    },
    list: {
      marginTop: 24,
      borderRadius: radius.large,
      borderWidth: 1,
      borderColor: colors.border,
      overflow: "hidden",
      backgroundColor:
        colors.white,
    },
    row: {
      minHeight: 74,
      paddingHorizontal: 13,
      borderBottomWidth: 1,
      borderBottomColor:
        colors.border,
      flexDirection: "row",
      alignItems: "center",
      gap: 11,
    },
    icon: {
      width: 40,
      height: 40,
      borderRadius: 13,
      backgroundColor:
        colors.surfaceSoft,
      alignItems: "center",
      justifyContent: "center",
    },
    number: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 11,
    },
    detail: {
      marginTop: 3,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
    },
    missed: {
      color: colors.error,
      fontFamily: fonts.semiBold,
    },
    time: {
      maxWidth: 70,
      fontFamily: fonts.regular,
      color: colors.softMuted,
      fontSize: 7,
      textAlign: "right",
    },
  });
