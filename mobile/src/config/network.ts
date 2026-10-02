import * as Linking from "expo-linking";

const DEFAULT_PORT = 8100;

function trimTrailingSlash(value: string) {
  return value.replace(/\/+$/, "");
}

function hostFromExpoUrl() {
  const expoUrl =
    Linking.createURL("/");

  const match =
    expoUrl.match(
      /^(?:exp|exps|http|https):\/\/([^/:]+)/i,
    );

  return match?.[1] ?? null;
}

export function getBtsUrl() {
  const configured =
    process.env
      .EXPO_PUBLIC_BTS_URL
      ?.trim() ||
    process.env
      .EXPO_PUBLIC_NETWORK_SIMULATOR_URL
      ?.trim();

  if (configured) {
    return trimTrailingSlash(
      configured,
    );
  }

  const host =
    hostFromExpoUrl();

  if (host) {
    return `ws://${host}:${DEFAULT_PORT}`;
  }

  return `ws://127.0.0.1:${DEFAULT_PORT}`;
}

export const getNetworkSimulatorUrl =
  getBtsUrl;
