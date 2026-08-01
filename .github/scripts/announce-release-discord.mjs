import { readFile } from "node:fs/promises";

const webhookUrl = (process.env.DISCORD_WEBHOOK_URL || "").trim();
const repoSlug = (process.env.GITHUB_REPOSITORY || "").trim();
const releaseTag = (process.env.RELEASE_TAG || "").trim();
const githubToken = (process.env.GITHUB_TOKEN || "").trim();

const defaultReleaseEmoji = ":video_game:";
const defaultMentions = "";
const releaseEmoji = (process.env.DISCORD_RELEASE_EMOJI || defaultReleaseEmoji).trim();
const mentions = (process.env.DISCORD_RELEASE_MENTIONS || defaultMentions).trim();
const feedbackChannel = (process.env.DISCORD_FEEDBACK_CHANNEL || "the openPREY community channels").trim();
const avatarUrl = (process.env.DISCORD_RELEASE_AVATAR_URL ||
  "https://raw.githubusercontent.com/themuffinator/openPREY/main/assets/img/avatar.png").trim();

function requireValue(value, name) {
  if (!value) {
    throw new Error(`${name} is required.`);
  }
}

function validateGitHubRepoSlug(value) {
  const normalized = value.trim();
  if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(normalized)) {
    throw new Error("GITHUB_REPOSITORY must be an owner/name repository slug.");
  }
  return normalized;
}

function validateHttpsUrl(value, label, allowedHosts = null) {
  const normalized = (value || "").trim();
  requireValue(normalized, label);

  let parsed;
  try {
    parsed = new URL(normalized);
  } catch (error) {
    throw new Error(`${label} must be an absolute https URL.`);
  }

  if (parsed.protocol !== "https:") {
    throw new Error(`${label} must use https.`);
  }
  if (parsed.username || parsed.password) {
    throw new Error(`${label} must not include credentials.`);
  }
  if (/[\u0000-\u0020\u007f<>()\[\]`]/u.test(normalized)) {
    throw new Error(`${label} contains characters that are unsafe in Discord Markdown.`);
  }
  if (allowedHosts && !allowedHosts.has(parsed.hostname.toLowerCase())) {
    throw new Error(`${label} host is not allowed: ${parsed.hostname}`);
  }
  return parsed.href;
}

function validateDiscordWebhookUrl(value) {
  const parsedUrl = validateHttpsUrl(value, "DISCORD_WEBHOOK_URL", new Set(["discord.com", "discordapp.com"]));
  const parsed = new URL(parsedUrl);
  if (!parsed.pathname.startsWith("/api/webhooks/")) {
    throw new Error("DISCORD_WEBHOOK_URL must point at a Discord webhook API path.");
  }
  return parsed.href;
}

async function loadReleaseFromEvent() {
  const eventPath = process.env.GITHUB_EVENT_PATH;
  if (!eventPath) {
    return null;
  }

  const rawEvent = await readFile(eventPath, "utf8");
  const event = JSON.parse(rawEvent);
  if (!event.release) {
    return null;
  }

  if (releaseTag && event.release.tag_name !== releaseTag) {
    return null;
  }

  return event.release;
}

async function fetchReleaseByTag() {
  const safeRepoSlug = validateGitHubRepoSlug(repoSlug);
  requireValue(releaseTag, "RELEASE_TAG");
  requireValue(githubToken, "GITHUB_TOKEN");

  const response = await fetch(
    `https://api.github.com/repos/${safeRepoSlug}/releases/tags/${encodeURIComponent(releaseTag)}`,
    {
      headers: {
        Accept: "application/vnd.github+json",
        Authorization: `Bearer ${githubToken}`,
        "X-GitHub-Api-Version": "2022-11-28",
      },
    },
  );

  if (!response.ok) {
    throw new Error(`GitHub release lookup failed: ${response.status} ${await response.text()}`);
  }

  return response.json();
}

async function loadRelease() {
  return (await loadReleaseFromEvent()) || fetchReleaseByTag();
}

function cleanReleaseName(release) {
  const rawName = (release.name || release.tag_name || "release").replace(/\s+/g, " ").trim();
  const unbrandedName = rawName.replace(/^(?:openq4|openprey)\b[\s:-]*/i, "").trim();
  return unbrandedName ? `openPREY ${unbrandedName}` : "openPREY";
}

function buildIntro(release, notes) {
  const lower = notes.toLowerCase();
  const mode = release.prerelease ? "prerelease" : "release";

  if (/(renderer|lighting|shadow|bloom|flare|opengl|performance|frame|gpu)/.test(lower)) {
    return `The new openPREY ${mode} is ready, with rendering and performance work that needs real gameplay miles across supported platforms.`;
  }

  if (/(linux|macos|windows|steam deck|sdl|controller|gamepad|package|installer)/.test(lower)) {
    return `The new openPREY ${mode} is ready, with platform and packaging improvements for cleaner installs and broader testing.`;
  }

  if (/(single-player|multiplayer|\bsp\b|\bmp\b|gameplay|map|asset|pk4|compatibility)/.test(lower)) {
    return `The new openPREY ${mode} is ready, focused on stock-asset compatibility and in-game behavior that should be tested with original Prey (2006) content.`;
  }

  return `The new openPREY ${mode} is ready. Grab a platform package, try it with the original Prey (2006) assets, and send back anything that looks off.`;
}

function extractHighlights(notes) {
  if (!notes) {
    return "";
  }

  const lines = notes.split("\n");
  const startIndex = lines.findIndex((line) => /^#{2,3}\s+Highlights\s*$/i.test(line.trim()));
  if (startIndex < 0) {
    return "";
  }

  const startLevel = lines[startIndex].trim().match(/^(#{2,3})/)?.[1].length || 3;
  const highlights = [];
  for (const line of lines.slice(startIndex + 1)) {
    const heading = line.trim().match(/^(#{1,6})\s+\S/);
    if (heading && heading[1].length <= startLevel) {
      break;
    }
    highlights.push(line);
  }

  return highlights.join("\n").replace(/\n{3,}/g, "\n\n").trim();
}

function limitText(text, maxLength) {
  if (text.length <= maxLength) {
    return text;
  }
  return `${text.slice(0, maxLength - 3).trimEnd()}...`;
}

function markdownLink(label, rawUrl) {
  return `[${label}](${validateHttpsUrl(rawUrl, `${label} URL`, new Set(["github.com"]))})`;
}

function findAssetBySuffix(assets, suffix) {
  return assets.find((asset) => asset.name.toLowerCase().endsWith(suffix));
}

function buildDownloadLinks(release) {
  const assets = release.assets || [];
  const desired = [
    ["windows-x64-setup.exe", "Windows x64 Installer"],
    ["windows-arm64-setup.exe", "Windows ARM64 Installer"],
    ["x86_64.appimage", "Linux x64 AppImage"],
    ["linux-x64.tar.xz", "Linux x64"],
    ["aarch64.appimage", "Linux ARM64 AppImage"],
    ["linux-arm64.tar.xz", "Linux ARM64"],
    ["macos-arm64-opengl.dmg", "macOS ARM64 OpenGL"],
    ["macos-arm64-opengl-unsigned.tar.gz", "macOS ARM64 OpenGL unsigned"],
    ["macos-arm64-opengl.tar.gz", "macOS ARM64 OpenGL"],
    ["macos-arm64-metal.dmg", "macOS ARM64 Metal"],
    ["macos-arm64-metal-unsigned.tar.gz", "macOS ARM64 Metal unsigned"],
    ["macos-arm64-metal.tar.gz", "macOS ARM64 Metal"],
  ];

  const links = [];
  const linuxArm64PreviewAppImage = findAssetBySuffix(assets, "preview-aarch64.appimage");
  const linuxArm64PreviewAsset = findAssetBySuffix(assets, "linux-arm64-preview.tar.xz");
  for (const [suffix, label] of desired) {
    if (suffix === "aarch64.appimage" && linuxArm64PreviewAppImage) {
      links.push(markdownLink("Linux ARM64 preview AppImage", linuxArm64PreviewAppImage.browser_download_url));
      continue;
    }
    if (suffix === "linux-arm64.tar.xz" && linuxArm64PreviewAsset) {
      links.push(markdownLink("Linux ARM64 preview", linuxArm64PreviewAsset.browser_download_url));
      continue;
    }
    const asset = findAssetBySuffix(assets, suffix);
    if (asset) {
      links.push(markdownLink(label, asset.browser_download_url));
    }
  }

  links.push(markdownLink("All downloads", release.html_url));
  return limitText(links.join("\n"), 1000);
}

async function postDiscordPayload(payload) {
  const safeWebhookUrl = validateDiscordWebhookUrl(webhookUrl);

  const response = await fetch(safeWebhookUrl, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(payload),
  });

  if (!response.ok) {
    throw new Error(`Discord webhook failed: ${response.status} ${await response.text()}`);
  }
}

async function main() {
  const release = await loadRelease();
  const releaseUrl = validateHttpsUrl(release.html_url, "release html_url", new Set(["github.com"]));
  const safeAvatarUrl = validateHttpsUrl(avatarUrl, "DISCORD_RELEASE_AVATAR_URL");
  const displayName = cleanReleaseName(release);
  const notes = (release.body || "").replace(/\r/g, "").trim();
  const intro = buildIntro(release, notes);
  const highlights = extractHighlights(notes);
  const detailsLink = `[Full release notes and downloads](${releaseUrl})`;
  const feedback = `Feedback is welcome in ${feedbackChannel}.`;
  const reservedLength = intro.length + feedback.length + detailsLink.length + 8;
  const highlightsSection = highlights
    ? limitText(`## Highlights\n\n${highlights}`, Math.max(200, 2200 - reservedLength))
    : "Release notes and downloads are available on GitHub.";

  const description = limitText([
    intro,
    feedback,
    highlightsSection,
    detailsLink,
  ].join("\n\n"), 2200);

  const headline = `${releaseEmoji} ${displayName} release published!${mentions ? ` ${mentions}` : ""}`.trim();

  const payload = {
    username: "openPREY Releases",
    avatar_url: safeAvatarUrl,
    allowed_mentions: { parse: ["roles"] },
    content: headline,
    embeds: [
      {
        title: displayName,
        url: releaseUrl,
        description,
        color: release.prerelease ? 0xd97a1f : 0x2d8f4e,
        fields: [
          { name: "Version", value: release.tag_name || releaseTag, inline: true },
          { name: "State", value: release.prerelease ? "Prerelease" : "Stable release", inline: true },
          { name: "Downloads", value: buildDownloadLinks(release) || `[Open release](${release.html_url})` },
        ],
        footer: { text: "openPREY - open-source engine and game code for Prey (2006)" },
        timestamp: release.published_at || new Date().toISOString(),
      },
    ],
  };

  await postDiscordPayload(payload);
}

main().catch((error) => {
  console.error(error.message);
  process.exit(1);
});
