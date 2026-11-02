# Ask companion

[РІвЂ С’ Wiki home](https://github.com/zaera/Pika_note/wiki/Home)

## One conversation, on both sides
Ask combines general questions, web search, your active records and read-only mail. The phone can send typed or spoken questions without Pika nearby. Long-press a message bubble to copy it. The conversation opens at its latest messages; date dividers help you find older exchanges.

History is retained, but model context is bounded: do not treat every old message as permanently present in each request. Renew starts a clean conversation using current personal records. It does not delete the records themselves. Full backups include the dialogue and available device context/history files.

## Keep notes as notes
Upper-button recording always creates a memory. To turn a memory into a plan, use Ask and identify the note by its subject, a phrase from it, when it was captured or вЂњthe latest memory.вЂќ When similar memories exist, ask for a summary first and clarify the one you mean.

## Useful requests
- вЂњCreate tasks and events from my latest memory about the website.вЂќ
- вЂњCreate a high-priority task to send the files by Friday at 2 pm.вЂќ
- вЂњCreate an event tomorrow at 7:30 pm for one hour.вЂќ
- вЂњCreate an all-day event from November 5 to November 8.вЂќ
- вЂњMark the task about the TV remote as completed.вЂќ
- вЂњCancel all events tomorrow about the workshop.вЂќ
- вЂњRestore the task about homework from the archive.вЂќ
- вЂњLook in the archive for the memory about my trip.вЂќ
- вЂњРџСЂРµРІСЂР°С‚Рё РїРѕСЃР»РµРґРЅСЋСЋ Р·Р°РјРµС‚РєСѓ Рѕ СЃР°Р№С‚Рµ РІ Р·Р°РґР°С‡Рё Рё СЃРѕР±С‹С‚РёСЏ.вЂќ
- вЂњРџРѕРјРµС‚СЊ Р·Р°РґР°С‡Сѓ РїСЂРѕ РїСѓР»СЊС‚ РєР°Рє РІС‹РїРѕР»РЅРµРЅРЅСѓСЋ.вЂќ

Relative dates use the capture/request date in Europe/Kyiv. Tasks without a requested deadline remain undated. Give a time or duration when it matters. Ambiguous requests may need clarification; check the resulting records after a batch operation.

## Changes and archive scope
Config в†’ Ask companion controls voice changes to existing records. Enable Allow voice changes to records for edit, complete, archive and restore workflows. Normal questions use active records; mention archive explicitly when you want historical data. Deleting a task through Ask means completing/archiving it in the record workflow, not purging its history. Permanent archive deletion is a separate confirmed action.

## Mail and web
Ask can fetch your configured filtered mail. Try вЂњRead my unread mail briefly.вЂќ Summaries aim for sender, subject and message, replacing links rather than speaking long URLs. Mail access is read only. General current-information questions can use web search; availability depends on phone internet, credentials and the provider. Source links appear with answers when returned.

## When an action fails
A failed or ambiguous plan should not be treated as completed. Check the records before repeating a create request. If a response says Try again, inspect Config в†’ Diagnostics for the cause, check internet/API access, then retry. The device and app keep bounded error journals with up to 200 entries.
