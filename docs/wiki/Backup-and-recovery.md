# Backup, recovery and ownership

[РІвЂ С’ Wiki home](Home)

## Create a complete backup
In Config в†’ Data & recovery choose the complete backup action. Connect Pika first so device settings and mirrored files are available. Choose a password of at least ten characters and confirm it. Save the .pika-backup file somewhere safe; choose Google Drive in Android's document picker for cloud storage when available.

The portable encrypted backup includes records, available WAVs, transcripts, Ask history, phone state and captured device/AI/Google settings. It is independent of the original phone's Android Keystore. The password is required on another phone. Keep both file and password: losing the password makes recovery impossible. Credentials are sensitive even inside an encrypted backup.

## Restore
Choose Restore, select the backup and enter its password. The app validates/decrypts it before replacing the phone library. Confirm applying the restored library to the connected Pika. Keep the phone/app available and the device awake until activation and synchronization finish. Large recordings make a full restore slower than routine sync.

Current device ownership and the current app PIN are retained instead of copied from an old owner. Wrong-password or damaged authenticated backups are rejected. A restore replaces the target library: it is not a merge of two independent libraries. Verify records and connected services afterward. Previous device data may be retained as a recovery copy; do not rely on that as your only backup.

## USB Mount SD
Connect Pika to a computer by USB and use Mount SD in Config while Companion can reach it. The card is exposed read-only for copying. Back up the complete notes folder including hidden files; raw SD copying alone does not include every phone/device setting. Eject the drive on the computer, then use the lower button to leave USB Backup. Never format the mounted card as part of a backup.

## Undo, archive cleanup and reset
Undo last change restores the available previous record state. Archive keeps completed tasks, past events and forgotten memories. Permanent archive cleanup removes retained archived content after confirmation. Factory reset is broader: it clears records, history and device settings. Download and verify a backup before any destructive operation.

## Forget Pika and a new owner
Forget Pika is destructive, not a simple disconnect. It clears device data and settings, removes the binding, clears this app's library and returns to onboarding after confirmation. Back up first. An owned device accepts its bound phone; a new owner pairs after the old owner forgets it. Some Android versions may leave a system Bluetooth entry that must be removed in Android settings.

Unpaired Pika shows its identity and pairing code rather than the library. The short PN serial is useful for choosing the right device. Bluetooth pairing, the app PIN and the encrypted backup password serve different purposes.

## Storage privacy
The current microSD contents are not encrypted at rest. Someone with the card may read its files. Portable complete backups are password-encrypted, but this does not encrypt the installed SD card. USB Mount is read-only access to those files.
