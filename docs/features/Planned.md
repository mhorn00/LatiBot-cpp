# What LatiBot will do

Features that are designed but not built, written the same way as
[what already exists](README.md): what each command does, its options, who may
run it, and how it should behave. The language model, which was here, is built
and documented there now.

**This is the document to correct.** Nothing here is written yet, so changing a
reply, an option name or a rule costs nothing now and costs a rewrite later. The
technical design behind each one — schemas, algorithms, the bugs being fixed —
is in [docs/porting/Porting_Plan_Final.md](../porting/Porting_Plan_Final.md);
this is the user-facing side of the same thing.

Command names, option names and reply wording are all proposals.

---

## Later, unscheduled

**Music.** The Java bot played from YouTube and direct links through LavaPlayer,
which has no C++ equivalent. The rebuild will use yt-dlp and ffmpeg, with the
queue redesigned around Discord's own playback markers rather than the
hand-rolled bookkeeping the Java version had. Music pauses while the bot speaks
and resumes afterwards. The commands stay **unregistered** until it works, so
the slash menu is not full of things that reply "not implemented".

**Emote statistics.** Which custom emoji actually get used. The Java version
re-read every message in every channel on each run, which is why it was slow;
this one will scan incrementally and remember where it got to.

**Appearance tracking.** Nickname history, but for how someone *looked* — server
avatar, name colour including gradient roles, decorations. Probably shown as a
generated image, since an embed cannot represent a gradient.

**Self-hosted embeds.** Downloading the linked content and hosting the embed
directly, so it survives the original being deleted or the mirror services
disappearing. Written up in
[docs/ideas/Self_Hosted_Embeds.md](../ideas/Self_Hosted_Embeds.md).
