import {
  Phone,
  PhoneIncoming,
  RadioTower,
  UserRound,
} from "lucide-react-native";
import {
  useState,
} from "react";
import {
  Pressable,
  ScrollView,
  StyleSheet,
  Text,
  View,
} from "react-native";
import {
  router,
} from "expo-router";
import {
  useSafeAreaInsets,
} from "react-native-safe-area-context";

import {
  formatPhoneNumber,
  useDialer,
} from "../context/DialerContext";
import {
  colors,
  fonts,
  radius,
} from "../theme";

const KEYS = [
  ["1", ""],
  ["2", "ABC"],
  ["3", "DEF"],
  ["4", "GHI"],
  ["5", "JKL"],
  ["6", "MNO"],
  ["7", "PQRS"],
  ["8", "TUV"],
  ["9", "WXYZ"],
  ["*", ""],
  ["0", "+"],
  ["#", ""],
] as const;

export default function DialerScreen() {
  const insets =
    useSafeAreaInsets();

  const {
    activeSubscriber,
    peerSubscriber,
    connectionStatus,
    peerOnline,
    resetSubscriber,
    startCall,
  } = useDialer();

  const [
    number,
    setNumber,
  ] =
    useState("");

  const append =
    (value: string) => {
      const current =
        number.replace(
          /\D/g,
          "",
        );

      if (
        value.match(/\d/) &&
        current.length >= 10
      ) {
        return;
      }

      setNumber(
        formatPhoneNumber(
          `${number}${value}`,
        ),
      );
    };

  const target =
    number ||
    peerSubscriber?.number ||
    "";

  const networkText =
    connectionStatus ===
    "connected"
      ? "Connected"
      : connectionStatus ===
        "connecting"
        ? "Connecting…"
        : "No connection";

  return (
    <ScrollView
      style={styles.root}
      contentContainerStyle={[
        styles.content,
        {
          paddingTop:
            Math.max(
              insets.top + 10,
              20,
            ),
        },
      ]}
      showsVerticalScrollIndicator={
        false
      }
    >
      <View style={styles.header}>
        <View>
          <Text style={styles.eyebrow}>
            YOUR NUMBER
          </Text>

          <Text style={styles.headerNumber}>
            {activeSubscriber?.number}
          </Text>
        </View>

        <Pressable
          style={styles.profileButton}
          onPress={() => {
            resetSubscriber();

            router.replace(
              "/subscriber-select" as any,
            );
          }}
        >
          <UserRound
            size={22}
            color={colors.white}
          />
        </Pressable>
      </View>

      <View style={styles.networkCard}>
        <View style={styles.networkIcon}>
          <RadioTower
            size={22}
            color={colors.charcoal}
          />
        </View>

        <View style={{ flex: 1 }}>
          <Text style={styles.networkTitle}>
            Mobile network
          </Text>

          <Text style={styles.networkSubtitle}>
            {networkText}
          </Text>
        </View>

        <Text style={styles.networkReady}>
          {connectionStatus ===
          "connected"
            ? "Ready"
            : "—"}
        </Text>
      </View>

      <View style={styles.numberArea}>
        <Text style={styles.numberLabel}>
          Dial number
        </Text>

        <Text
          style={[
            styles.number,
            !target &&
              styles.placeholder,
          ]}
          numberOfLines={1}
          adjustsFontSizeToFit
        >
          {target ||
            "Enter number"}
        </Text>

        {peerSubscriber ? (
          <Pressable
            style={styles.quickDial}
            onPress={() =>
              setNumber(
                peerSubscriber.number,
              )
            }
          >
            <Text style={styles.quickDialText}>
              {peerOnline
                ? "Available"
                : "Not available"}
              {"  •  "}
              {peerSubscriber.number}
            </Text>
          </Pressable>
        ) : null}
      </View>

      <View style={styles.keypad}>
        {KEYS.map(
          ([digit, letters]) => (
            <Pressable
              key={digit}
              style={({
                pressed,
              }) => [
                styles.key,
                pressed &&
                  styles.keyPressed,
              ]}
              onPress={() =>
                append(digit)
              }
            >
              <Text style={styles.keyDigit}>
                {digit}
              </Text>

              <Text style={styles.keyLetters}>
                {letters || " "}
              </Text>
            </Pressable>
          ),
        )}
      </View>

      <View style={styles.actions}>
        <View
          style={styles.secondaryAction}
        >
          <PhoneIncoming
            size={21}
            color={colors.charcoal}
          />

          <Text style={styles.secondaryLabel}>
            Ready for calls
          </Text>
        </View>

        <Pressable
          style={[
            styles.callButton,
            connectionStatus !==
              "connected" &&
              styles.callButtonDisabled,
          ]}
          onPress={() => {
            if (!target) {
              return;
            }

            startCall(target);
          }}
        >
          <Phone
            size={28}
            color={colors.white}
          />
        </Pressable>

        <Pressable
          style={styles.secondaryAction}
          onPress={() => {
            const digits =
              number.replace(
                /\D/g,
                "",
              );

            setNumber(
              formatPhoneNumber(
                digits.slice(
                  0,
                  -1,
                ),
              ),
            );
          }}
        >
          <Text style={styles.deleteGlyph}>
            ⌫
          </Text>

          <Text style={styles.secondaryLabel}>
            Delete
          </Text>
        </Pressable>
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
    header: {
      minHeight: 66,
      flexDirection: "row",
      alignItems: "center",
      justifyContent:
        "space-between",
      gap: 12,
    },
    eyebrow: {
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 8,
      letterSpacing: 1.2,
    },
    headerNumber: {
      marginTop: 3,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 21,
    },
    profileButton: {
      width: 46,
      height: 46,
      borderRadius: 15,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    networkCard: {
      marginTop: 10,
      padding: 14,
      borderWidth: 1,
      borderColor: colors.border,
      borderRadius: radius.large,
      backgroundColor:
        colors.surfaceSoft,
      flexDirection: "row",
      alignItems: "center",
      gap: 11,
    },
    networkIcon: {
      width: 42,
      height: 42,
      borderRadius: 13,
      backgroundColor:
        colors.white,
      alignItems: "center",
      justifyContent: "center",
    },
    networkTitle: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 12,
    },
    networkSubtitle: {
      marginTop: 3,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
    },
    networkReady: {
      fontFamily: fonts.bold,
      color: colors.charcoalSoft,
      fontSize: 9,
    },
    numberArea: {
      minHeight: 124,
      alignItems: "center",
      justifyContent: "center",
      paddingHorizontal: 12,
    },
    numberLabel: {
      fontFamily: fonts.semiBold,
      color: colors.softMuted,
      fontSize: 9,
    },
    number: {
      marginTop: 8,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 30,
      letterSpacing: 1,
    },
    placeholder: {
      color: colors.softMuted,
      fontSize: 23,
    },
    quickDial: {
      marginTop: 11,
      paddingHorizontal: 11,
      paddingVertical: 7,
      borderRadius: radius.pill,
      backgroundColor:
        colors.surface,
    },
    quickDialText: {
      fontFamily: fonts.semiBold,
      color: colors.muted,
      fontSize: 8,
    },
    keypad: {
      alignSelf: "center",
      width: "100%",
      maxWidth: 340,
      flexDirection: "row",
      flexWrap: "wrap",
      justifyContent:
        "space-between",
      rowGap: 11,
    },
    key: {
      width: "30%",
      aspectRatio: 1.12,
      maxHeight: 74,
      borderRadius: 22,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
      alignItems: "center",
      justifyContent: "center",
    },
    keyPressed: {
      backgroundColor:
        "#ECECEC",
    },
    keyDigit: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 24,
      lineHeight: 28,
    },
    keyLetters: {
      marginTop: 1,
      minHeight: 9,
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 7,
      letterSpacing: 1.1,
    },
    actions: {
      marginTop: 22,
      flexDirection: "row",
      alignItems: "center",
      justifyContent:
        "space-between",
    },
    callButton: {
      width: 66,
      height: 66,
      borderRadius: 22,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    callButtonDisabled: {
      opacity: 0.45,
    },
    secondaryAction: {
      width: 92,
      alignItems: "center",
      gap: 6,
    },
    deleteGlyph: {
      fontFamily: fonts.bold,
      color: colors.charcoal,
      fontSize: 25,
      lineHeight: 28,
    },
    secondaryLabel: {
      fontFamily: fonts.semiBold,
      color: colors.muted,
      fontSize: 8,
      textAlign: "center",
    },
  });
