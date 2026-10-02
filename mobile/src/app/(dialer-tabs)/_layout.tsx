import {
  Clock3,
  MessageSquareText,
  Phone,
  RadioTower,
} from "lucide-react-native";
import {
  Redirect,
  Tabs,
} from "expo-router";

import {
  useDialer,
} from "../../context/DialerContext";
import {
  colors,
  fonts,
} from "../../theme";

export default function DialerTabsLayout() {
  const {
    activeSubscriber,
  } = useDialer();

  if (!activeSubscriber) {
    return (
      <Redirect
        href={"/subscriber-select" as any}
      />
    );
  }

  return (
    <Tabs
      screenOptions={{
        headerShown: false,
        tabBarActiveTintColor:
          colors.charcoal,
        tabBarInactiveTintColor:
          colors.softMuted,
        tabBarLabelStyle: {
          fontFamily: fonts.bold,
          fontSize: 9,
          marginTop: 2,
        },
        tabBarStyle: {
          height: 72,
          paddingTop: 8,
          paddingBottom: 10,
          borderTopWidth: 1,
          borderTopColor:
            colors.border,
          backgroundColor:
            colors.white,
        },
        tabBarHideOnKeyboard:
          true,
      }}
    >
      <Tabs.Screen
        name="index"
        options={{
          title: "Phone",
          tabBarIcon: ({
            color,
            focused,
          }) => (
            <Phone
              size={
                focused
                  ? 22
                  : 21
              }
              color={color}
              strokeWidth={
                focused
                  ? 2.5
                  : 2
              }
            />
          ),
        }}
      />

      <Tabs.Screen
        name="messages"
        options={{
          title: "Messages",
          tabBarIcon: ({
            color,
            focused,
          }) => (
            <MessageSquareText
              size={
                focused
                  ? 22
                  : 21
              }
              color={color}
              strokeWidth={
                focused
                  ? 2.5
                  : 2
              }
            />
          ),
        }}
      />

      <Tabs.Screen
        name="recents"
        options={{
          title: "Recents",
          tabBarIcon: ({
            color,
            focused,
          }) => (
            <Clock3
              size={
                focused
                  ? 22
                  : 21
              }
              color={color}
              strokeWidth={
                focused
                  ? 2.5
                  : 2
              }
            />
          ),
        }}
      />

      <Tabs.Screen
        name="network"
        options={{
          title: "Connection",
          tabBarIcon: ({
            color,
            focused,
          }) => (
            <RadioTower
              size={
                focused
                  ? 22
                  : 21
              }
              color={color}
              strokeWidth={
                focused
                  ? 2.5
                  : 2
              }
            />
          ),
        }}
      />
    </Tabs>
  );
}
