// copy to secrets.h (same folder) to switch the LIVE mode on. Without secrets.h the board runs REPLAY only.
#define WIFI_SSID     "Friedrichshafener_4_2.4"
#define WIFI_PASSWORD "your wifi password"
// more networks (optional), same pattern as the cinema board:
// #define WIFI_EXTRA { {"Name2", "pw2"}, {"Name3", "pw3"} }
// the file the GitHub workflow publishes (replace USER/REPO):
#define DATA_URL "https://raw.githubusercontent.com/USER/REPO/main/data/live.txt"
