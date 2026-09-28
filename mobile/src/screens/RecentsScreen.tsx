import {
  PhoneIncoming,
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
  type RecentCall,
  useDialer,
} from "../context/DialerContext";
import {
  colors,
  fonts,
  radius,
} from "../theme";

function CallIcon({
  call,
}: {
  call: RecentCall;
}) {
  if (
    call.direction ===
    "incoming"
  ) {
    return (
      <PhoneIncoming
        size={19}
        color={colors.charcoal}
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
    recentCalls,
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
        Calls made or received on this phone.
      </Text>

      {recentCalls.length ? (
        <View style={styles.list}>
          {recentCalls.map(
            (call) => (
              <View
                key={call.id}
                style={styles.row}
              >
                <View style={styles.icon}>
                  <CallIcon
                    call={call}
                  />
                </View>

                <View style={{ flex: 1 }}>
                  <Text style={styles.number}>
                    {call.number}
                  </Text>

                  <Text style={styles.detail}>
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
      ) : (
        <View style={styles.empty}>
          <Text style={styles.emptyTitle}>
            No recent calls
          </Text>

          <Text style={styles.emptyText}>
            Your calls will appear here.
          </Text>
        </View>
      )}
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
    time: {
      maxWidth: 70,
      fontFamily: fonts.regular,
      color: colors.softMuted,
      fontSize: 7,
      textAlign: "right",
    },
    empty: {
      marginTop: 24,
      minHeight: 150,
      padding: 20,
      borderRadius: radius.large,
      borderWidth: 1,
      borderColor: colors.border,
      backgroundColor:
        colors.surfaceSoft,
      alignItems: "center",
      justifyContent: "center",
    },
    emptyTitle: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 12,
    },
    emptyText: {
      marginTop: 5,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 9,
    },
  });
