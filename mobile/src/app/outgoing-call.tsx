import {
  router,
  useLocalSearchParams,
} from "expo-router";
import {
  PhoneCall,
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
  useDialer,
} from "../context/DialerContext";
import {
  colors,
  fonts,
  radius,
} from "../theme";

export default function OutgoingCallScreen() {
  const insets =
    useSafeAreaInsets();

  const params =
    useLocalSearchParams<{
      number?: string;
    }>();

  const {
    activeSubscriber,
  } = useDialer();

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
              insets.top + 12,
              28,
            ),
          paddingBottom:
            Math.max(
              insets.bottom + 18,
              28,
            ),
        },
      ]}
    >
      <View style={styles.top}>
        <Text style={styles.eyebrow}>
          OUTGOING CALL
        </Text>

        <View style={styles.avatar}>
          <PhoneCall
            size={33}
            color={colors.white}
          />
        </View>

        <Text style={styles.number}>
          {number}
        </Text>

        <Text style={styles.status}>
          Calling through simulated base station…
        </Text>

        <View style={styles.routePill}>
          <Signal
            size={14}
            color={colors.charcoal}
          />

          <Text style={styles.routeText}>
            {activeSubscriber?.number}
            {"  "}• LTE • ABS-01
          </Text>
        </View>
      </View>

      <View>
        <Pressable
          style={styles.connect}
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
          <PhoneCall
            size={20}
            color={colors.white}
          />

          <Text style={styles.connectText}>
            Simulate answer
          </Text>
        </Pressable>

        <Pressable
          style={styles.cancel}
          onPress={() =>
            router.replace(
              "/(dialer-tabs)" as any,
            )
          }
        >
          <PhoneOff
            size={20}
            color={colors.error}
          />

          <Text style={styles.cancelText}>
            Cancel call
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
      paddingHorizontal: 22,
      backgroundColor:
        colors.canvas,
      justifyContent:
        "space-between",
    },
    top: {
      paddingTop: 58,
      alignItems: "center",
    },
    eyebrow: {
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 9,
      letterSpacing: 1.4,
    },
    avatar: {
      width: 94,
      height: 94,
      marginTop: 32,
      borderRadius: 32,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    number: {
      marginTop: 23,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 27,
    },
    status: {
      marginTop: 8,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 11,
    },
    routePill: {
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
    routeText: {
      fontFamily: fonts.semiBold,
      color: colors.charcoalSoft,
      fontSize: 8,
    },
    connect: {
      height: 54,
      borderRadius: radius.card,
      backgroundColor:
        colors.charcoal,
      flexDirection: "row",
      alignItems: "center",
      justifyContent: "center",
      gap: 9,
    },
    connectText: {
      fontFamily: fonts.bold,
      color: colors.white,
      fontSize: 11,
    },
    cancel: {
      height: 54,
      marginTop: 10,
      borderRadius: radius.card,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
      flexDirection: "row",
      alignItems: "center",
      justifyContent: "center",
      gap: 9,
    },
    cancelText: {
      fontFamily: fonts.bold,
      color: colors.error,
      fontSize: 11,
    },
  });
