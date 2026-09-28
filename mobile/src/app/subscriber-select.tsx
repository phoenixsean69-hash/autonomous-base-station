import {
  router,
} from "expo-router";
import {
  ArrowRight,
  RadioTower,
  ShieldCheck,
  Smartphone,
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
  SUBSCRIBERS,
  useDialer,
} from "../context/DialerContext";
import {
  colors,
  fonts,
  radius,
} from "../theme";

export default function SubscriberSelectScreen() {
  const insets =
    useSafeAreaInsets();

  const {
    selectSubscriber,
  } = useDialer();

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
              26,
            ),
        },
      ]}
    >
      <View style={styles.header}>
        <View style={styles.logo}>
          <RadioTower
            size={27}
            color={colors.white}
          />
        </View>

        <Text style={styles.eyebrow}>
          ABS CONNECT
        </Text>

        <Text style={styles.title}>
          Choose a number for this phone
        </Text>

        <Text style={styles.subtitle}>
          Use the other number on a second phone
          or emulator to make and receive calls.
        </Text>
      </View>

      <View style={styles.cards}>
        {SUBSCRIBERS.map(
          (
            subscriber,
            index,
          ) => (
            <Pressable
              key={subscriber.id}
              style={({ pressed }) => [
                styles.card,
                pressed &&
                  styles.cardPressed,
              ]}
              onPress={() => {
                selectSubscriber(
                  subscriber,
                );

                router.replace(
                  "/(dialer-tabs)" as any,
                );
              }}
            >
              <View style={styles.avatar}>
                <Smartphone
                  size={23}
                  color={colors.white}
                />
              </View>

              <View style={{ flex: 1 }}>
                <Text style={styles.name}>
                  Phone {index + 1}
                </Text>

                <Text style={styles.number}>
                  {subscriber.number}
                </Text>

                <View style={styles.badge}>
                  <ShieldCheck
                    size={13}
                    color={colors.charcoal}
                  />

                  <Text style={styles.badgeText}>
                    Demo mobile number
                  </Text>
                </View>
              </View>

              <ArrowRight
                size={20}
                color={colors.charcoal}
              />
            </Pressable>
          ),
        )}
      </View>

      <View style={styles.note}>
        <Text style={styles.noteTitle}>
          Project demonstration
        </Text>

        <Text style={styles.noteText}>
          Each device should use a different
          number. After selection, the phone
          connects automatically.
        </Text>
      </View>
    </View>
  );
}

const styles =
  StyleSheet.create({
    root: {
      flex: 1,
      paddingHorizontal: 20,
      backgroundColor:
        colors.canvas,
      justifyContent:
        "space-between",
    },
    header: {
      paddingTop: 28,
      alignItems: "center",
    },
    logo: {
      width: 64,
      height: 64,
      borderRadius: 20,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
      marginBottom: 18,
    },
    eyebrow: {
      fontFamily: fonts.bold,
      color: colors.softMuted,
      fontSize: 9,
      letterSpacing: 1.4,
    },
    title: {
      marginTop: 10,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 25,
      lineHeight: 31,
      textAlign: "center",
    },
    subtitle: {
      marginTop: 9,
      maxWidth: 360,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 11,
      lineHeight: 17,
      textAlign: "center",
    },
    cards: {
      gap: 12,
    },
    card: {
      minHeight: 118,
      padding: 16,
      borderWidth: 1,
      borderColor: colors.border,
      borderRadius: radius.large,
      backgroundColor:
        colors.white,
      flexDirection: "row",
      alignItems: "center",
      gap: 13,
    },
    cardPressed: {
      backgroundColor:
        colors.surfaceSoft,
    },
    avatar: {
      width: 50,
      height: 50,
      borderRadius: 16,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    name: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 14,
    },
    number: {
      marginTop: 3,
      fontFamily: fonts.semiBold,
      color: colors.muted,
      fontSize: 12,
      letterSpacing: 0.4,
    },
    badge: {
      alignSelf: "flex-start",
      marginTop: 10,
      paddingHorizontal: 9,
      paddingVertical: 6,
      borderRadius: radius.pill,
      backgroundColor:
        colors.surface,
      flexDirection: "row",
      alignItems: "center",
      gap: 5,
    },
    badgeText: {
      fontFamily: fonts.semiBold,
      color: colors.charcoalSoft,
      fontSize: 8,
    },
    note: {
      padding: 14,
      borderRadius: radius.large,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
    },
    noteTitle: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 10,
    },
    noteText: {
      marginTop: 4,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 9,
      lineHeight: 14,
    },
  });
