import {
  router,
} from "expo-router";
import {
  useEffect,
  useState,
} from "react";
import {
  ArrowRight,
  RadioTower,
  ShieldCheck,
  Smartphone,
} from "lucide-react-native";
import {
  Pressable,
  ScrollView,
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
  getBtsHttpUrl,
} from "../config/network";
import {
  colors,
  fonts,
  radius,
} from "../theme";

// ABS SUBSCRIBER NUMBER LOCK V1
function normalizeSubscriberNumber(
  value: string,
) {
  return value.replace(
    /\D/g,
    "",
  );
}

export default function SubscriberSelectScreen() {
  const insets =
    useSafeAreaInsets();

  const {
    selectSubscriber,
  } = useDialer();

  const [
    takenNumbers,
    setTakenNumbers,
  ] =
    useState<Set<string>>(
      new Set(),
    );

  useEffect(() => {
    let disposed = false;

    const refresh =
      async () => {
        try {
          const response =
            await fetch(
              `${getBtsHttpUrl()}/state`,
            );

          if (!response.ok) {
            return;
          }

          const state =
            await response.json();

          if (disposed) {
            return;
          }

          const next =
            new Set<string>();

          if (
            Array.isArray(
              state?.subscribers,
            )
          ) {
            for (
              const item of
              state.subscribers
            ) {
              const raw =
                typeof item ===
                "string"
                  ? item
                  : String(
                      item?.number ??
                        "",
                    );

              const normalized =
                normalizeSubscriberNumber(
                  raw,
                );

              if (normalized) {
                next.add(
                  normalized,
                );
              }
            }
          }

          setTakenNumbers(
            next,
          );
        }
        catch {
          // Keep the selector usable if BTS state
          // cannot be refreshed momentarily.
        }
      };

    void refresh();

    const timer =
      setInterval(
        () => {
          void refresh();
        },
        1500,
      );

    return () => {
      disposed = true;
      clearInterval(timer);
    };
  }, []);

  return (
    <ScrollView
      style={styles.root}
      contentContainerStyle={[
        styles.content,
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
      showsVerticalScrollIndicator={false}
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
          Choose one of six demo numbers. Use a
          different number on each phone or emulator.
        </Text>
      </View>

      <View style={styles.cards}>
        {SUBSCRIBERS.map(
          (
            subscriber,
            index,
          ) => {
            const taken =
              takenNumbers.has(
                normalizeSubscriberNumber(
                  subscriber.number,
                ),
              );

            return (
            <Pressable
              key={subscriber.id}
              disabled={taken}
              style={({ pressed }) => [
                styles.card,
                taken &&
                  styles.cardTaken,
                pressed &&
                  !taken &&
                  styles.cardPressed,
              ]}
              onPress={() => {
                if (taken) {
                  return;
                }

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
                    {taken
                      ? "Number already in use"
                      : "Available demo number"}
                  </Text>
                </View>
              </View>

              <ArrowRight
                size={20}
                color={
                  taken
                    ? colors.softMuted
                    : colors.charcoal
                }
              />
            </Pressable>
            );
          },
        )}
      </View>

      <View style={styles.note}>
        <Text style={styles.noteTitle}>
          Project demonstration
        </Text>

        <Text style={styles.noteText}>
          Default quick pairs: 001 ↔ 002,
          003 ↔ 004, and 005 ↔ 006. You can
          still dial any of the six numbers.
        </Text>
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
      paddingHorizontal: 20,
      gap: 18,
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
      gap: 10,
    },
    card: {
      minHeight: 94,
      padding: 14,
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
    cardTaken: {
      opacity: 0.46,
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
