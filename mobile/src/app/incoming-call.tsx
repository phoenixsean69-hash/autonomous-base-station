import {
  router,
  useLocalSearchParams,
} from "expo-router";
import {
  Phone,
  PhoneIncoming,
  PhoneOff,
  Signal,
} from "lucide-react-native";
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

export default function IncomingCallScreen() {
  const insets =
    useSafeAreaInsets();

  const params =
    useLocalSearchParams<{
      from?: string;
    }>();

  const number =
    String(
      params.from ||
      "0712 000 001",
    );

  return (
    <View
      style={[
        styles.root,
        {
          paddingTop:
            Math.max(
              insets.top + 12,
              28,
            ),
          paddingBottom:
            Math.max(
              insets.bottom + 24,
              30,
            ),
        },
      ]}
    >
      <View style={styles.top}>
        <Text style={styles.eyebrow}>
          INCOMING CALL
        </Text>

        <View style={styles.avatar}>
          <PhoneIncoming
            size={34}
            color={colors.white}
          />
        </View>

        <Text style={styles.number}>
          {number}
        </Text>

        <Text style={styles.status}>
          Simulated subscriber calling
        </Text>

        <View style={styles.networkPill}>
          <Signal
            size={14}
            color={colors.charcoal}
          />

          <Text style={styles.networkText}>
            LTE • Base Station ABS-01
          </Text>
        </View>
      </View>

      <View style={styles.actions}>
        <View style={styles.actionItem}>
          <Pressable
            style={[
              styles.circle,
              styles.decline,
            ]}
            onPress={() =>
              router.replace(
                "/(dialer-tabs)" as any,
              )
            }
          >
            <PhoneOff
              size={27}
              color={colors.white}
            />
          </Pressable>

          <Text style={styles.actionLabel}>
            Decline
          </Text>
        </View>

        <View style={styles.actionItem}>
          <Pressable
            style={[
              styles.circle,
              styles.answer,
            ]}
            onPress={() =>
              router.replace({
                pathname:
                  "/active-call" as any,
                params: {
                  number,
                },
              })
            }
          >
            <Phone
              size={27}
              color={colors.white}
            />
          </Pressable>

          <Text style={styles.actionLabel}>
            Answer
          </Text>
        </View>
      </View>
    </View>
  );
}

const styles =
  StyleSheet.create({
    root: {
      flex: 1,
      paddingHorizontal: 24,
      backgroundColor:
        colors.canvas,
      justifyContent:
        "space-between",
    },
    top: {
      paddingTop: 55,
      alignItems: "center",
    },
    eyebrow: {
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 9,
      letterSpacing: 1.5,
    },
    avatar: {
      width: 96,
      height: 96,
      marginTop: 32,
      borderRadius: 34,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    number: {
      marginTop: 24,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 28,
    },
    status: {
      marginTop: 7,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 11,
    },
    networkPill: {
      marginTop: 18,
      paddingHorizontal: 12,
      paddingVertical: 8,
      borderRadius: radius.pill,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
      flexDirection: "row",
      alignItems: "center",
      gap: 7,
    },
    networkText: {
      fontFamily: fonts.semiBold,
      color: colors.charcoalSoft,
      fontSize: 8,
    },
    actions: {
      paddingBottom: 18,
      flexDirection: "row",
      justifyContent:
        "space-evenly",
    },
    actionItem: {
      alignItems: "center",
      gap: 10,
    },
    circle: {
      width: 72,
      height: 72,
      borderRadius: 25,
      alignItems: "center",
      justifyContent: "center",
    },
    decline: {
      backgroundColor:
        colors.error,
    },
    answer: {
      backgroundColor:
        colors.charcoal,
    },
    actionLabel: {
      fontFamily: fonts.bold,
      color: colors.muted,
      fontSize: 9,
    },
  });
