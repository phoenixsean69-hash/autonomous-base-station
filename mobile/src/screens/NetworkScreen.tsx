import {
  Clock3,
  RadioTower,
  ShieldCheck,
  Signal,
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
  delayLabel,
  interruptionLabel,
  qualityLabel,
  signalLabel,
} from "../utils/networkLabels";
import {
  colors,
  fonts,
  radius,
} from "../theme";

function Metric({
  icon: Icon,
  label,
  value,
}: {
  icon: any;
  label: string;
  value: string;
}) {
  return (
    <View style={styles.metric}>
      <View style={styles.metricIcon}>
        <Icon
          size={18}
          color={colors.charcoal}
        />
      </View>

      <Text style={styles.metricValue}>
        {value}
      </Text>

      <Text style={styles.metricLabel}>
        {label}
      </Text>
    </View>
  );
}

export default function NetworkScreen() {
  const insets =
    useSafeAreaInsets();

  const {
    activeSubscriber,
    peerSubscriber,
    connectionStatus,
    peerOnline,
    metrics,
  } = useDialer();

  const connected =
    connectionStatus ===
    "connected";

  const statusText =
    connected
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
              insets.top + 12,
              24,
            ),
        },
      ]}
    >
      <Text style={styles.eyebrow}>
        CONNECTION
      </Text>

      <Text style={styles.title}>
        Mobile network
      </Text>

      <View style={styles.cellCard}>
        <View style={styles.cellIcon}>
          <RadioTower
            size={24}
            color={colors.white}
          />
        </View>

        <View style={{ flex: 1 }}>
          <Text style={styles.cellTitle}>
            {statusText}
          </Text>

          <Text style={styles.cellSubtitle}>
            {activeSubscriber?.number}
          </Text>
        </View>

        <View
          style={[
            styles.ready,
            !connected &&
              styles.readyMuted,
          ]}
        >
          <ShieldCheck
            size={13}
            color={colors.white}
          />

          <Text style={styles.readyText}>
            {connected
              ? "READY"
              : "OFFLINE"}
          </Text>
        </View>
      </View>

      <Text style={styles.sectionTitle}>
        Connection quality
      </Text>

      <View style={styles.metrics}>
        <Metric
          icon={Signal}
          label="Signal"
          value={signalLabel(
            metrics?.rssi_dbm,
          )}
        />

        <Metric
          icon={ShieldCheck}
          label="Call quality"
          value={qualityLabel(
            metrics?.quality,
          )}
        />

        <Metric
          icon={Clock3}
          label="Call delay"
          value={delayLabel(
            metrics?.latency_ms,
          )}
        />

        <Metric
          icon={Signal}
          label="Interruptions"
          value={interruptionLabel(
            metrics,
          )}
        />
      </View>

      <Text style={styles.sectionTitle}>
        Phone details
      </Text>

      <View style={styles.routeCard}>
        <View style={styles.routeRow}>
          <Text style={styles.routeLabel}>
            Your number
          </Text>
          <Text style={styles.routeValue}>
            {activeSubscriber?.number}
          </Text>
        </View>

        <View style={styles.divider} />

        <View style={styles.routeRow}>
          <Text style={styles.routeLabel}>
            Other demo phone
          </Text>
          <Text style={styles.routeValue}>
            {peerSubscriber?.number}
          </Text>
        </View>

        <View style={styles.divider} />

        <View style={styles.routeRow}>
          <Text style={styles.routeLabel}>
            Other phone
          </Text>
          <Text style={styles.routeValue}>
            {peerOnline
              ? "Available"
              : "Unavailable"}
          </Text>
        </View>
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
      letterSpacing: 1.3,
    },
    title: {
      marginTop: 5,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 25,
    },
    cellCard: {
      marginTop: 18,
      padding: 15,
      borderRadius: radius.large,
      backgroundColor:
        colors.surfaceSoft,
      borderWidth: 1,
      borderColor: colors.border,
      flexDirection: "row",
      alignItems: "center",
      gap: 11,
    },
    cellIcon: {
      width: 48,
      height: 48,
      borderRadius: 15,
      backgroundColor:
        colors.charcoal,
      alignItems: "center",
      justifyContent: "center",
    },
    cellTitle: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 13,
    },
    cellSubtitle: {
      marginTop: 3,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
    },
    ready: {
      paddingHorizontal: 9,
      paddingVertical: 7,
      borderRadius: radius.pill,
      backgroundColor:
        colors.charcoal,
      flexDirection: "row",
      alignItems: "center",
      gap: 5,
    },
    readyMuted: {
      opacity: 0.45,
    },
    readyText: {
      fontFamily: fonts.bold,
      color: colors.white,
      fontSize: 7,
      letterSpacing: 0.8,
    },
    sectionTitle: {
      marginTop: 22,
      marginBottom: 9,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 13,
    },
    metrics: {
      flexDirection: "row",
      flexWrap: "wrap",
      gap: 8,
    },
    metric: {
      width: "48.5%",
      minHeight: 104,
      padding: 12,
      borderRadius: radius.large,
      backgroundColor:
        colors.white,
      borderWidth: 1,
      borderColor: colors.border,
    },
    metricIcon: {
      width: 34,
      height: 34,
      borderRadius: 10,
      backgroundColor:
        colors.surfaceSoft,
      alignItems: "center",
      justifyContent: "center",
    },
    metricValue: {
      marginTop: 10,
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 18,
    },
    metricLabel: {
      marginTop: 2,
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 8,
    },
    routeCard: {
      paddingHorizontal: 13,
      borderRadius: radius.large,
      backgroundColor:
        colors.white,
      borderWidth: 1,
      borderColor: colors.border,
    },
    routeRow: {
      minHeight: 58,
      flexDirection: "row",
      alignItems: "center",
      justifyContent:
        "space-between",
      gap: 14,
    },
    routeLabel: {
      fontFamily: fonts.regular,
      color: colors.muted,
      fontSize: 9,
    },
    routeValue: {
      fontFamily: fonts.bold,
      color: colors.text,
      fontSize: 10,
    },
    divider: {
      height: 1,
      backgroundColor:
        colors.border,
    },
  });
