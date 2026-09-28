import {
  Mulish_400Regular,
  Mulish_600SemiBold,
  Mulish_700Bold,
  useFonts,
} from "@expo-google-fonts/mulish";
import { Stack } from "expo-router";
import * as SplashScreen from "expo-splash-screen";
import { StatusBar } from "expo-status-bar";
import { useEffect } from "react";
import {
  SafeAreaProvider,
} from "react-native-safe-area-context";

import {
  DialerProvider,
} from "../context/DialerContext";
import {
  colors,
} from "../theme";

SplashScreen.preventAutoHideAsync().catch(
  () => {},
);

export default function RootLayout() {
  const [fontsLoaded] = useFonts({
    Mulish_400Regular,
    Mulish_600SemiBold,
    Mulish_700Bold,
  });

  useEffect(() => {
    if (fontsLoaded) {
      SplashScreen.hideAsync().catch(
        () => {},
      );
    }
  }, [fontsLoaded]);

  if (!fontsLoaded) {
    return null;
  }

  return (
    <SafeAreaProvider
      style={{
        flex: 1,
        backgroundColor: colors.canvas,
      }}
    >
      <StatusBar style="dark" />

      <DialerProvider>
        <Stack
          screenOptions={{
            headerShown: false,
            contentStyle: {
              backgroundColor:
                colors.canvas,
            },
          }}
        />
      </DialerProvider>
    </SafeAreaProvider>
  );
}
