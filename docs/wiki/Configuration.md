# Fine-tune your Pika

[← Wiki home](https://github.com/zaera/Pika_note/wiki/Home)

## AI and voice
Config → AI & voice changes your OpenAI key and reply voice. Leave a saved-key field empty to retain the key. Cedar is male; Coral is female. Answers follow Russian or English questions. A saved key is not a guarantee that the provider account has usable billing, quota or access.

## Buttons and extra screens
Assign actions independently for upper/lower single, double, triple and hold. Use original defaults resets the mapping. Assign Weather, Month calendar or Mail to a combination if you want it on the device. Weather is refreshed when opening Weather. Mail is refreshed when opening Mail rather than on every wake. The device does not need saved Wi-Fi networks in the phone-companion mode; internet services run through the phone.

## Sleep timer
Config → Device & controls → Sleep timer sets 1–5 minutes of inactivity (default: 1 minute). Save to queue the setting for Pika; it applies when connected and survives restart. Active recording and synchronization finish safely before sleep. Clock remains visible. Complete backups include this device setting; restore with Pika connected to apply it again.

## Sound, reminders and activity light
Sound & melody controls speaker volume, reminder melody and per-scenario notification patterns. Choose No sound for any scenario you want silent. Patterns cover recording start, successful Ask, saved memory, errors / no connection, increased unread mail, wake readiness and sleep. Use the separate Test on Pika selector to preview a melody on the connected device. A disconnected or sleeping Pika cannot play a live test.

Event reminders live in the sound section. Enable them, set minutes before a timed event and alert duration. Cached events allow device reminders without a live internet connection. Lower / Back stops an alert. All-day events are not timed reminders. Confirm the device clock, volume and reminder settings with a short test event before relying on alerts.

The optional activity LED lights while recording or communicating and turns off when idle/ready. It does not control the hardware charging LED.

## Appearance and access
Choose light or dark theme in Config. The optional four-digit PIN locks only the app; it is separate from Bluetooth pairing. Five quick taps on the Today case illustration open its colors. Use the continuous palette or a HEX value for the body and buttons. Default returns the original SVG colors. Both awake and sleeping illustrations share the palette; sleep retains its dimming.

## Sync and status
Pull down from the top to request sync. The thin orange bar and status text show progress. Ordinary sync exchanges changes; Force full sync in Config verifies the whole library. Library edits on the phone are saved locally immediately, then queued for Pika. During a full restore, the device illustration shows RESTORING and dashes until activation is acknowledged.

## Google Calendar
1. Enable Calendar API in your Google Cloud project. Create a service account and download its JSON key.
2. Share the target Google calendar with that JSON's client_email and allow event changes.
3. In Google Calendar settings, copy the explicit Calendar ID under Integrate calendar. Do not use “primary” for the service account.
4. In Config → Google Calendar, choose the key, enter the ID and save. Test the connection / import with Pika connected if requested by the app.

Local event changes are exported to Google. Import missing Google events is a separate action. Edits to an already imported event in Google are not a general two-way merge in this version. Configured means credentials are present; verify a real operation before assuming access works.

## Gmail
1. Enable Gmail API. Configure OAuth consent and add your account as a test user if the Google project is in Testing.
2. Create an OAuth client of type Desktop app. On your computer save its JSON as config/gmail-oauth-client.json in the source checkout you use.
3. Run python tools/gmail_connect.py from that checkout, sign in and grant read-only mail access.
4. Import the generated config/gmail-account.json in Config → Gmail, then save.

Up to four filters accept an exact sender address or a domain such as @example.com. Mail shows up to four recent unread incoming conversations from the last seven days. Empty filters use Inbox / Primary. Spam, Trash, drafts and your own replies are excluded. Reading does not mark mail read. Only the latest incoming content in a conversation is used. Testing-mode Google authorizations can expire; reconnect when Google rejects a token.

## Weather
Set one location name and latitude/longitude in Config. Assign Weather to a device combination. Opening the screen requests fresh Open-Meteo data through the phone; failures retain the saved forecast and its timestamp. The screen includes current conditions, high/low and upcoming forecast slots. Dates use Europe/Kyiv.
