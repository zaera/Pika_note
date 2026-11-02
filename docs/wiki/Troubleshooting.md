# Troubleshooting

[РІвЂ С’ Wiki home](https://github.com/zaera/Pika_note/wiki/Home)

## Connected, but changes are still queued
Keep Pika awake and the app available. Check the top status and orange progress line. A connection is not the same as a completed sync. Pull down once to request another attempt. Large WAVs or a full restore take longer; ordinary edits should use incremental exchange. Use Force full sync only when you need a complete verification.

## No connection after wake
Allow a moment for Bluetooth reconnection, check phone Bluetooth and Companion permissions, and make sure another test client is not holding the device connection. Keep the app allowed to run in the background. Saved memories remain queued if the phone is absent. If necessary, use the both-button companion reconnect action from Today.

## Try again / missing transcript
Check phone internet, your AI key and provider availability. Keep an offline recording until recognition finishes. If you manually edited its text, automatic recognition deliberately leaves your changes alone. Open Diagnostics, choose Phone app or Pika SD, and view/download the recent errors. Error journals are bounded, not full audio recordings.

## No sound
Wake and connect Pika, increase volume and choose a sound other than No sound. Test the selected melody from Sound & melody. Notification scenarios have independent patterns. A test command sent while disconnected cannot be heard on the device.

## Calendar or Gmail fails
A saved configuration is not proof of Google access. For Calendar check the explicit ID, calendar sharing with the service-account email and Calendar API. For Gmail check Desktop OAuth credentials, the signed-in account, read-only grant and token expiry. Reconnect the account with the helper when needed. Do not paste keys or tokens into public issues.

## Device recovery
From normal running firmware, hold both buttons for about five seconds and release to request a software restart. This cannot recover ROM bootloader mode or every hard CPU failure. If a USB/serial tool reports a busy port, close the application using it. Use the firmware image for your exact board/display; a color display is not interchangeable with a monochrome driver just because the cable fits.

## Before reporting a problem
Note the firmware/app versions, short device serial, action performed, whether Pika was asleep, phone internet/Bluetooth state and the relevant recent diagnostic entry. Redact personal text and credentials. Back up before trying reset or re-pairing.
