#include "ImGuiMedia.hh"

#include "ImGuiCpp.hh"
#include "ImGuiManager.hh"
#include "ImGuiOpenFile.hh"
#include "ImGuiUtils.hh"

#include "CartridgeSlotManager.hh"
#include "CassettePlayerCLI.hh"
#include "DiskImageCLI.hh"
#include "DiskImageUtils.hh"
#include "DiskManipulator.hh"
#include "FilePool.hh"
#include "HardwareConfig.hh"
#include "HD.hh"
#include "IDECDROM.hh"
#include "MSXCliComm.hh"
#include "MSXCommandController.hh"
#include "MSXRomCLI.hh"
#include "Reactor.hh"
#include "RealDrive.hh"
#include "RomDatabase.hh"
#include "RomInfo.hh"

#include "join.hh"
#include "one_of.hh"
#include "ranges.hh"
#include "StringOp.hh"
#include "unreachable.hh"
#include "view.hh"

#include <CustomFont.h>
#include <imgui.h>
#include <imgui_stdlib.h>

#include <algorithm>
#include <iomanip>
#include <memory>
#include <sstream>

using namespace std::literals;

namespace openmsx {

void ImGuiMedia::save(ImGuiTextBuffer& buf)
{
	savePersistent(buf, *this, persistentElements);

	auto saveItem = [&](const MediaItem& item, zstring_view name) {
		if (item.name.empty()) return;
		buf.appendf("%s.name=%s\n", name.c_str(), item.name.c_str());
		for (const auto& patch : item.ipsPatches) {
			buf.appendf("%s.patch=%s\n", name.c_str(), patch.c_str());
		}
		if (item.romType != ROM_UNKNOWN) {
			buf.appendf("%s.romType=%s\n", name.c_str(),
				std::string(RomInfo::romTypeToName(item.romType)).c_str());
		}
	};
	auto saveGroup = [&](const ItemGroup& group, zstring_view name) {
		saveItem(group.edit, name);
		auto recentName = tmpStrCat(name, ".recent");
		for (const auto& item : group.recent) {
			saveItem(item, recentName);
		}
		// don't save patchIndex
	};

	std::string name;
	name = "diska";
	for (const auto& info : diskMediaInfo) {
		saveGroup(info.groups[0], tmpStrCat(name, ".image"));
		saveGroup(info.groups[1], tmpStrCat(name, ".dirAsDsk"));
		// don't save groups[2]
		//if (info.select) buf.appendf("%s.select=%d\n", name.c_str(), info.select);
		if (info.show) buf.appendf("%s.show=1\n", name.c_str());
		name.back()++;
	}

	name = "carta";
	for (const auto& info : cartridgeMediaInfo) {
		saveGroup(info.groups[0], tmpStrCat(name, ".rom"));
		saveGroup(info.groups[1], tmpStrCat(name, ".extension"));
		//if (info.select) buf.appendf("%s.select=%d\n", name.c_str(), info.select);
		if (info.show) buf.appendf("%s.show=1\n", name.c_str());
		name.back()++;
	}

	name = "hda";
	for (const auto& info : hdMediaInfo) {
		saveGroup(info, name);
		name.back()++;
	}

	name = "cda";
	for (const auto& info : cdMediaInfo) {
		saveGroup(info, name);
		name.back()++;
	}

	if (cassetteMediaInfo.show) buf.append("cassette.show=1\n");
	saveGroup(cassetteMediaInfo.group, "cassette");

	saveGroup(extensionMediaInfo, "extension");
	saveGroup(laserdiscMediaInfo, "laserdisc");
}

void ImGuiMedia::loadLine(std::string_view name, zstring_view value)
{
	auto get = [&](std::string_view prefix, auto& array) -> std::remove_reference_t<decltype(array[0])>* {
		if ((name.size() >= (prefix.size() + 2)) && name.starts_with(prefix) && (name[prefix.size() + 1] == '.')) {
			char c = name[prefix.size()];
			if (('a' <= c) && (c < char('a' + array.size()))) {
				return &array[c - 'a'];
			}
		}
		return nullptr;
	};
	auto loadItem = [&](MediaItem& item, std::string_view suffix) {
		if (suffix == "name") {
			item.name = value;
		} else if (suffix == "patch") {
			item.ipsPatches.emplace_back(value);
		} else if (suffix == "romType") {
			if (auto type = RomInfo::nameToRomType(value); type != ROM_UNKNOWN) {
				item.romType = type;
			}
		}
	};
	auto loadGroup = [&](ItemGroup& group, std::string_view suffix) {
		if (suffix.starts_with("recent.")) {
			if (suffix == "recent.name" && !group.recent.full()) {
				group.recent.push_back(MediaItem{});
			}
			if (!group.recent.empty()) {
				loadItem(group.recent.back(), suffix.substr(7));
			}
		} else {
			loadItem(group.edit, suffix);
		}
	};

	if (loadOnePersistent(name, value, *this, persistentElements)) {
		// already handled
	} else if (auto* disk = get("disk", diskMediaInfo)) {
		auto suffix = name.substr(6);
		if (suffix.starts_with("image.")) {
			loadGroup(disk->groups[0], suffix.substr(6));
		} else if (suffix.starts_with("dirAsDsk.")) {
			loadGroup(disk->groups[1], suffix.substr(9));
		} else if (suffix == "select") {
			if (auto i = StringOp::stringTo<int>(value)) {
				if (SELECT_DISK_IMAGE <= *i && *i <= SELECT_RAMDISK) {
					disk->select = *i;
				}
			}
		} else if (suffix == "show") {
			disk->show = StringOp::stringToBool(value);
		}
	} else if (auto* cart = get("cart", cartridgeMediaInfo)) {
		auto suffix = name.substr(6);
		if (suffix.starts_with("rom.")) {
			loadGroup(cart->groups[0], suffix.substr(4));
		} else if (suffix.starts_with("extension.")) {
			loadGroup(cart->groups[1], suffix.substr(10));
		} else if (suffix == "select") {
			if (auto i = StringOp::stringTo<int>(value)) {
				if (SELECT_ROM_IMAGE <= *i && *i <= SELECT_EXTENSION) {
					cart->select = *i;
				}
			}
		} else if (suffix == "show") {
			cart->show = StringOp::stringToBool(value);
		}
	} else if (auto* hd = get("hd", hdMediaInfo)) {
		loadGroup(*hd, name.substr(4));
	} else if (auto* cd = get("cd", cdMediaInfo)) {
		loadGroup(*cd, name.substr(4));
	} else if (name.starts_with("cassette.")) {
		auto suffix = name.substr(9);
		if (suffix == "show") {
			cassetteMediaInfo.show = StringOp::stringToBool(value);
		} else {
			loadGroup(cassetteMediaInfo.group, suffix);
		}
	} else if (name.starts_with("extension.")) {
		loadGroup(extensionMediaInfo, name.substr(10));
	} else if (name.starts_with("laserdisc.")) {
		loadGroup(laserdiscMediaInfo, name.substr(10));
	}
}

static std::string buildFilter(std::string_view description, std::span<const std::string_view> extensions)
{
	auto formatExtensions = [&]() -> std::string {
		if (extensions.size() <= 3) {
			return join(view::transform(extensions,
			                [](const auto& ext) { return strCat("*.", ext); }),
			       ' ');
		} else {
			return join(extensions, ',');
		}
	};
	return strCat(
		description, " (", formatExtensions(), "){",
		join(view::transform(extensions,
		                     [](const auto& ext) { return strCat('.', ext); }),
		     ','),
		",.gz,.zip}");
}

std::string ImGuiMedia::diskFilter()
{
	return buildFilter("Disk images", DiskImageCLI::getExtensions());
}

static std::string romFilter()
{
	return buildFilter("ROM images", MSXRomCLI::getExtensions());
}

static std::string cassetteFilter()
{
	return buildFilter("Tape images", CassettePlayerCLI::getExtensions());
}

static std::string hdFilter()
{
	return buildFilter("Hard disk images", std::array{"dsk"sv});
}

static std::string cdFilter()
{
	return buildFilter("CDROM images", std::array{"iso"sv});
}

template<std::invocable<const std::string&> DisplayFunc = std::identity>
static std::string display(const ImGuiMedia::MediaItem& item, DisplayFunc displayFunc = {})
{
	std::string result = displayFunc(item.name);
	if (item.romType != ROM_UNKNOWN) {
		strAppend(result, " (", RomInfo::romTypeToName(item.romType), ')');
	}
	if (auto n = item.ipsPatches.size()) {
		strAppend(result, " (+", n, " patch", (n == 1 ? "" : "es"), ')');
	}
	return result;
}

std::vector<ImGuiMedia::ExtensionInfo>& ImGuiMedia::getAllExtensions()
{
	if (extensionInfo.empty()) {
		extensionInfo = parseAllConfigFiles<ExtensionInfo>(manager, "extensions", {"Manufacturer"sv, "Product code"sv, "Name"sv});
	}
	return extensionInfo;
}

void ImGuiMedia::resetExtensionInfo()
{
	extensionInfo.clear();
}

const std::string& ImGuiMedia::getTestResult(ExtensionInfo& info)
{
	if (!info.testResult) {
		info.testResult.emplace(); // empty string (for now)
		if (info.configName == one_of("advram", "Casio_KB-7", "Casio_KB-10")) {
			// HACK: These only work in specific machines (e.g. with specific slot/memory layout)
			// Report these as working because they don't depend on external ROM files.
			return info.testResult.value();
		}

		auto& reactor = manager.getReactor();
		manager.executeDelayed([&reactor, &info]() mutable {
			// don't create extra mb while drawing
			try {
				std::optional<MSXMotherBoard> mb;
				mb.emplace(reactor);
				// Non C-BIOS machine (see below) might e.g.
				// generate warnings about conflicting IO ports.
				mb->getMSXCliComm().setSuppressMessages(true);
				try {
					mb->loadMachine("C-BIOS_MSX1");
				} catch (MSXException& e1) {
					// Incomplete installation!! Missing C-BIOS machines!
					// Do a minimal attempt to recover.
					try {
						if (auto* current = reactor.getMotherBoard()) {
							mb.emplace(reactor); // need to recreate the motherboard
							mb->getMSXCliComm().setSuppressMessages(true);
							mb->loadMachine(std::string(current->getMachineName()));
						} else {
							throw e1;
						}
					} catch (MSXException&) {
						// if this also fails, then prefer the original error
						throw e1;
					}
				}
				auto ext = mb->loadExtension(info.configName, "any");
				mb->insertExtension(info.configName, std::move(ext));
				assert(info.testResult->empty());
			} catch (MSXException& e) {
				info.testResult = e.getMessage(); // error
			}
		});
	}
	return info.testResult.value();
}


ImGuiMedia::ExtensionInfo* ImGuiMedia::findExtensionInfo(std::string_view config)
{
	auto& allExtensions = getAllExtensions();
	auto it = ranges::find(allExtensions, config, &ExtensionInfo::configName);
	return (it != allExtensions.end()) ? std::to_address(it) : nullptr;
}

std::string ImGuiMedia::displayNameForExtension(std::string_view config)
{
	const auto* info = findExtensionInfo(config);
	return info ? info->displayName
	            : std::string(config); // normally shouldn't happen
}

std::string ImGuiMedia::displayNameForRom(const std::string& filename, bool compact)
{
	auto& reactor = manager.getReactor();
	if (auto sha1 = reactor.getFilePool().getSha1Sum(filename)) {
		auto& database = reactor.getSoftwareDatabase();
		if (const auto* romInfo = database.fetchRomInfo(*sha1)) {
			if (auto title = romInfo->getTitle(database.getBufferStart());
				!title.empty()) {
				return std::string(title);
			}
		}
	}
	return compact ? std::string(FileOperations::getFilename(filename))
	               : filename;
}

std::string ImGuiMedia::displayNameForHardwareConfig(const HardwareConfig& config, bool compact)
{
	if (config.getType() == HardwareConfig::Type::EXTENSION) {
		return displayNameForExtension(config.getConfigName());
	} else {
		return displayNameForRom(std::string(config.getRomFilename()), compact); // ROM filename
	}
}

std::string ImGuiMedia::displayNameForSlotContent(const CartridgeSlotManager& slotManager, unsigned slotNr, bool compact)
{
	if (const auto* config = slotManager.getConfigForSlot(slotNr)) {
		return displayNameForHardwareConfig(*config, compact);
	}
	return "Empty";
}

std::string ImGuiMedia::slotAndNameForHardwareConfig(const CartridgeSlotManager& slotManager, const HardwareConfig& config)
{
	auto slot = slotManager.findSlotWith(config);
	std::string result = slot
		? strCat(char('A' + *slot), " (", slotManager.getPsSsString(*slot), "): ")
		: "I/O-only: ";
	strAppend(result, displayNameForHardwareConfig(config));
	return result;
}

std::string ImGuiMedia::displayNameForDriveContent(unsigned drive, bool compact)
{
	auto cmd = makeTclList(tmpStrCat("disk", char('a' + drive)));
	std::string_view display;
	if (auto result = manager.execute(cmd)) {
		display = result->getListIndexUnchecked(1).getString();
	}
	return display.empty() ? "Empty"
	                       : std::string(compact ? FileOperations::getFilename(display)
	                                             : display);
}

void ImGuiMedia::printExtensionInfo(ExtensionInfo& info)
{
	const auto& test = getTestResult(info);
	bool ok = test.empty();
	if (ok) {
		im::Table("##extension-info", 2, [&]{
			ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

			for (const auto& [desc, value_] : info.configInfo) {
				const auto& value = value_; // clang workaround
				if (ImGui::TableNextColumn()) {
					ImGui::TextUnformatted(desc);
				}
				if (ImGui::TableNextColumn()) {
					im::TextWrapPos(ImGui::GetFontSize() * 35.0f, [&]{
						ImGui::TextUnformatted(value);
					});
				}
			}
		});
	} else {
		im::StyleColor(ImGuiCol_Text, getColor(imColor::ERROR), [&]{
			im::TextWrapPos(ImGui::GetFontSize() * 35.0f, [&] {
				ImGui::TextUnformatted(test);
			});
		});
	}
}

void ImGuiMedia::extensionTooltip(ExtensionInfo& info)
{
	im::ItemTooltip([&]{
		printExtensionInfo(info);
	});
}

bool ImGuiMedia::drawExtensionFilter()
{
	std::string filterDisplay = "filter";
	if (!filterType.empty() || !filterString.empty()) strAppend(filterDisplay, ':');
	if (!filterType.empty()) strAppend(filterDisplay, ' ', filterType);
	if (!filterString.empty()) strAppend(filterDisplay, ' ', filterString);
	strAppend(filterDisplay, "###filter");
	bool newFilterOpen = filterOpen;
	im::TreeNode(filterDisplay.c_str(), &newFilterOpen, [&]{
		displayFilterCombo(filterType, "Type", getAllExtensions());
		ImGui::InputText(ICON_IGFD_SEARCH, &filterString);
		simpleToolTip("A list of substrings that must be part of the extension.\n"
				"\n"
				"For example: enter 'ko' to search for 'Konami' extensions. "
				"Then refine the search by appending '<space>sc' to find the 'Konami SCC' extension.");
	});
	bool changed = filterOpen != newFilterOpen;
	filterOpen = newFilterOpen;
	return changed;
}

void ImGuiMedia::showMenu(MSXMotherBoard* motherBoard)
{
	im::Menu("Media", motherBoard != nullptr, [&]{
		auto& interp = manager.getInterpreter();

		enum class Status { NONE, ITEM, SEPARATOR };
		using enum Status;
		Status status = NONE;

		auto endGroup = [&] {
			if (status == ITEM) status = SEPARATOR;
		};
		auto elementInGroup = [&] {
			if (status == SEPARATOR) {
				ImGui::Separator();
			}
			status = ITEM;
		};

		auto showCurrent = [&](TclObject current, std::string_view type) {
			if (current.empty()) {
				ImGui::StrCat("Current: no ", type, " inserted");
			} else {
				ImGui::StrCat("Current: ", current.getString());
			}
			ImGui::Separator();
		};

		auto showRecent = [&](std::string_view mediaName, ItemGroup& group,
		                      function_ref<std::string(const std::string&)> displayFunc = std::identity{},
		                      const std::function<void(const std::string&)>& toolTip = {}) {
			if (!group.recent.empty()) {
				im::Indent([&] {
					im::Menu(strCat("Recent##", mediaName).c_str(), [&]{
						int count = 0;
						for (const auto& item : group.recent) {
							auto d = strCat(display(item, displayFunc), "##", count++);
							if (ImGui::MenuItem(d.c_str())) {
								group.edit = item;
								insertMedia(mediaName, group);
							}
							if (toolTip) toolTip(item.name);
						}
					});
				});
			}
		};

		// cartA / extX
		elementInGroup();
		auto& slotManager = motherBoard->getSlotManager();
		bool anySlot = false;
		for (auto i : xrange(CartridgeSlotManager::MAX_SLOTS)) {
			if (!slotManager.slotExists(i)) continue;
			anySlot = true;
			auto displayName = strCat("Cartridge Slot ", char('A' + i));
			ImGui::MenuItem(displayName.c_str(), nullptr, &cartridgeMediaInfo[i].show);
			simpleToolTip([&]{ return displayNameForSlotContent(slotManager, i); });
		}
		if (!anySlot) {
			ImGui::TextDisabled("No cartridge slots present");
		}
		endGroup();

		// extensions (needed for I/O-only extensions, or when you don't care about the exact slot)
		elementInGroup();
		im::Menu("Extensions", [&]{
			auto mediaName = "ext"sv;
			auto& group = extensionMediaInfo;
			im::Menu("Insert", [&]{
				ImGui::TextUnformatted("Select extension to insert in the first free slot"sv);
				HelpMarker("Note that some extensions are I/O only and will not occupy any cartridge slot when inserted. "
				           "These can only be removed via the 'Media > Extensions > Remove' menu. "
				           "To insert (non I/O-only) extensions in a specific slot, use the 'Media > Cartridge Slot' menu.");
				drawExtensionFilter();

				auto& allExtensions = getAllExtensions();
				auto filteredExtensions = to_vector(xrange(allExtensions.size()));
				applyComboFilter("Type", filterType, allExtensions, filteredExtensions);
				applyDisplayNameFilter(filterString, allExtensions, filteredExtensions);

				float width = 40.0f * ImGui::GetFontSize();
				float height = 10.25f * ImGui::GetTextLineHeightWithSpacing();
				im::ListBox("##list", {width, height}, [&]{
					im::ListClipper(filteredExtensions.size(), [&](int i) {
						auto& ext = allExtensions[filteredExtensions[i]];
						bool ok = getTestResult(ext).empty();
						im::StyleColor(!ok, ImGuiCol_Text, getColor(imColor::ERROR), [&]{
							if (ImGui::Selectable(ext.displayName.c_str())) {
								group.edit.name = ext.configName;
								insertMedia(mediaName, group);
								ImGui::CloseCurrentPopup();
							}
							extensionTooltip(ext);
						});
					});
				});
			});

			showRecent(mediaName, group,
				[this](const std::string& config) { // displayFunc
					return displayNameForExtension(config);
				},
				[this](const std::string& e) { // tooltip
					if (auto* info = findExtensionInfo(e)) {
						extensionTooltip(*info);
					}
				});

			ImGui::Separator();

			const auto& extensions = motherBoard->getExtensions();
			im::Disabled(extensions.empty(), [&]{
				im::Menu("Remove", [&]{
					int count = 0;
					for (const auto& ext : extensions) {
						auto name = strCat(slotAndNameForHardwareConfig(slotManager, *ext), "##", count++);
						if (ImGui::Selectable(name.c_str())) {
							manager.executeDelayed(makeTclList("remove_extension", ext->getName()));
						}
						if (auto* info = findExtensionInfo(ext->getConfigName())) {
							extensionTooltip(*info);
						}
					}
				});
			});
		});
		endGroup();

		// diskX
		elementInGroup();
		auto drivesInUse = RealDrive::getDrivesInUse(*motherBoard);
		bool anyDrive = false;
		for (auto i : xrange(RealDrive::MAX_DRIVES)) {
			if (!(*drivesInUse)[i]) continue;
			anyDrive = true;
			auto displayName = strCat("Disk Drive ", char('A' + i));
			ImGui::MenuItem(displayName.c_str(), nullptr, &diskMediaInfo[i].show);
			simpleToolTip([&] { return displayNameForDriveContent(i); });
		}
		if (!anyDrive) {
			ImGui::TextDisabled("No disk drives present");
		}
		endGroup();

		// cassetteplayer
		elementInGroup();
		if (auto cmdResult = manager.execute(TclObject("cassetteplayer"))) {
			ImGui::MenuItem("Tape Deck", nullptr, &cassetteMediaInfo.show);
			simpleToolTip([&]() -> std::string {
				auto tip = cmdResult->getListIndexUnchecked(1).getString();
				return !tip.empty() ? std::string(tip) : "Empty";
			});
		} else {
			ImGui::TextDisabled("No cassette port present");
		}
		endGroup();

		// hdX
		auto hdInUse = HD::getDrivesInUse(*motherBoard);
		std::string hdName = "hdX";
		for (auto i : xrange(HD::MAX_HD)) {
			if (!(*hdInUse)[i]) continue;
			hdName.back() = char('a' + i);
			auto displayName = strCat("Hard Disk ", char('A' + i));
			if (auto cmdResult = manager.execute(TclObject(hdName))) {
				elementInGroup();
				auto& group = hdMediaInfo[i];
				im::Menu(displayName.c_str(), [&]{
					auto currentImage = cmdResult->getListIndex(interp, 1);
					showCurrent(currentImage, "hard disk");
					bool powered = motherBoard->isPowered();
					im::Disabled(powered, [&]{
						if (ImGui::MenuItem("Select hard disk image...")) {
							manager.openFile->selectFile(
								"Select image for " + displayName,
								hdFilter(),
								[this, &group, hdName](const auto& fn) {
									group.edit.name = fn;
									this->insertMedia(hdName, group);
								},
								currentImage.getString());
						}
					});
					if (powered) {
						HelpMarker("Hard disk image cannot be switched while the MSX is powered on.");
					}
					im::Disabled(powered, [&]{
						showRecent(hdName, group);
					});
				});
			}
		}
		endGroup();

		// cdX
		auto cdInUse = IDECDROM::getDrivesInUse(*motherBoard);
		std::string cdName = "cdX";
		for (auto i : xrange(IDECDROM::MAX_CD)) {
			if (!(*cdInUse)[i]) continue;
			cdName.back() = char('a' + i);
			auto displayName = strCat("CDROM Drive ", char('A' + i));
			if (auto cmdResult = manager.execute(TclObject(cdName))) {
				elementInGroup();
				auto& group = cdMediaInfo[i];
				im::Menu(displayName.c_str(), [&]{
					auto currentImage = cmdResult->getListIndex(interp, 1);
					showCurrent(currentImage, "CDROM");
					if (ImGui::MenuItem("Eject", nullptr, false, !currentImage.empty())) {
						manager.executeDelayed(makeTclList(cdName, "eject"));
					}
					if (ImGui::MenuItem("Insert CDROM image...")) {
						manager.openFile->selectFile(
							"Select CDROM image for " + displayName,
							cdFilter(),
							[this, &group, cdName](const auto& fn) {
								group.edit.name = fn;
								this->insertMedia(cdName, group);
							},
							currentImage.getString());
					}
					showRecent(cdName, group);
				});
			}
		}
		endGroup();

		// laserdisc
		if (auto cmdResult = manager.execute(TclObject("laserdiscplayer"))) {
			elementInGroup();
			im::Menu("LaserDisc Player", [&]{
				auto currentImage = cmdResult->getListIndex(interp, 1);
				showCurrent(currentImage, "laserdisc");
				if (ImGui::MenuItem("eject", nullptr, false, !currentImage.empty())) {
					manager.executeDelayed(makeTclList("laserdiscplayer", "eject"));
				}
				if (ImGui::MenuItem("Insert LaserDisc image...")) {
					manager.openFile->selectFile(
						"Select LaserDisc image",
						buildFilter("LaserDisc images", std::array<std::string_view, 1>{"ogv"}),
						[this](const auto& fn) {
							laserdiscMediaInfo.edit.name = fn;
							this->insertMedia("laserdiscplayer", laserdiscMediaInfo);
						},
						currentImage.getString());
				}
				showRecent("laserdiscplayer", laserdiscMediaInfo);
			});
		}
		endGroup();
	});
}

void ImGuiMedia::paint(MSXMotherBoard* motherBoard)
{
	if (!motherBoard) return;

	auto drivesInUse = RealDrive::getDrivesInUse(*motherBoard);
	for (auto i : xrange(RealDrive::MAX_DRIVES)) {
		if (!(*drivesInUse)[i]) continue;
		if (diskMediaInfo[i].show) {
			diskMenu(i);
		}
	}

	auto& slotManager = motherBoard->getSlotManager();
	for (auto i : xrange(CartridgeSlotManager::MAX_SLOTS)) {
		if (!slotManager.slotExists(i)) continue;
		if (cartridgeMediaInfo[i].show) {
			cartridgeMenu(i);
		}
	}

	if (cassetteMediaInfo.show) {
		if (auto cmdResult = manager.execute(TclObject("cassetteplayer"))) {
			cassetteMenu(*cmdResult);
		}
	}
}

static TclObject getPatches(const TclObject& cmdResult)
{
	return cmdResult.getOptionalDictValue(TclObject("patches")).value_or(TclObject{});
}

static void printPatches(const TclObject& patches)
{
	if (!patches.empty()) {
		ImGui::TextUnformatted("IPS patches:"sv);
		im::Indent([&]{
			for (const auto& patch : patches) {
				ImGui::TextUnformatted(patch);
			}
		});
	}
}

static std::string leftClip(std::string_view s, float maxWidth)
{
	auto fullWidth = ImGui::CalcTextSize(s).x;
	if (fullWidth <= maxWidth) return std::string(s);

	maxWidth -= ImGui::CalcTextSize("..."sv).x;
	if (maxWidth <= 0.0f) return "...";

	auto len = s.size();
	auto num = *ranges::lower_bound(xrange(len), maxWidth, {},
		[&](size_t n) { return ImGui::CalcTextSize(s.substr(len - n)).x; });
	return strCat("...", s.substr(len - num));
}

bool ImGuiMedia::selectRecent(ItemGroup& group, function_ref<std::string(const std::string&)> displayFunc, float width) const
{
	bool interacted = false;
	ImGui::SetNextItemWidth(-width);
	const auto& style = ImGui::GetStyle();
	auto textWidth = ImGui::GetContentRegionAvail().x - (3.0f * style.FramePadding.x + ImGui::GetFrameHeight() + width);
	auto preview = leftClip(displayFunc(group.edit.name), textWidth);
	im::Combo("##recent", preview.c_str(), [&]{
		int count = 0;
		for (auto& item : group.recent) {
			auto d = strCat(display(item, displayFunc), "##", count++);
			if (ImGui::Selectable(d.c_str())) {
				group.edit = item;
				interacted = true;
			}
		}
	});
	interacted |= ImGui::IsItemActive();
	return interacted;
}

static float calcButtonWidth(std::string_view text1, const char* text2)
{
	const auto& style = ImGui::GetStyle();
	float width = style.ItemSpacing.x + 2.0f * style.FramePadding.x + ImGui::CalcTextSize(text1).x;
	if (text2) {
		width += style.ItemSpacing.x + 2.0f * style.FramePadding.x + ImGui::CalcTextSize(text2).x;
	}
	return width;
}

bool ImGuiMedia::selectImage(ItemGroup& group, const std::string& title,
                             function_ref<std::string()> createFilter, zstring_view current,
                             function_ref<std::string(const std::string&)> displayFunc,
                             const std::function<void()>& createNewCallback)
{
	bool interacted = false;
	im::ID("file", [&]{
		auto width = calcButtonWidth(ICON_IGFD_FOLDER_OPEN, createNewCallback ? ICON_IGFD_ADD : nullptr);
		interacted |= selectRecent(group, displayFunc, width);
		if (createNewCallback) {
			ImGui::SameLine();
			if (ImGui::Button(ICON_IGFD_ADD)) {
				interacted = true;
				createNewCallback();
			}
			simpleToolTip("Create new file");
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_IGFD_FOLDER_OPEN)) {
			interacted = true;
			manager.openFile->selectFile(
				title,
				createFilter(),
				[&](const auto& fn) { group.edit.name = fn; },
				current);
		}
		simpleToolTip("Browse file");
	});
	return interacted;
}

bool ImGuiMedia::selectDirectory(ItemGroup& group, const std::string& title, zstring_view current,
                                 const std::function<void()>& createNewCallback)
{
	bool interacted = false;
	im::ID("directory", [&]{
		auto width = calcButtonWidth(ICON_IGFD_FOLDER_OPEN, createNewCallback ? ICON_IGFD_ADD : nullptr);
		interacted |= selectRecent(group, std::identity{}, width);
		if (createNewCallback) {
			ImGui::SameLine();
			if (ImGui::Button(ICON_IGFD_ADD)) {
				interacted = true;
				createNewCallback();
			}
			simpleToolTip("Create new directory");
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_IGFD_FOLDER_OPEN)) {
			interacted = true;
			manager.openFile->selectDirectory(
				title,
				[&](const auto& fn) { group.edit.name = fn; },
				current);
		}
		simpleToolTip("Browse directory");
	});
	return interacted;
}

bool ImGuiMedia::selectMapperType(const char* label, RomType& romType)
{
	bool interacted = false;
	bool isAutoDetect = romType == ROM_UNKNOWN;
	constexpr const char* autoStr = "auto detect";
	std::string current = isAutoDetect ? autoStr : std::string(RomInfo::romTypeToName(romType));
	im::Combo(label, current.c_str(), [&]{
		if (ImGui::Selectable(autoStr, isAutoDetect)) {
			interacted = true;
			romType = ROM_UNKNOWN;
		}
		int count = 0;
		for (const auto& romInfo : RomInfo::getRomTypeInfo()) {
			bool selected = romType == static_cast<RomType>(count);
			if (ImGui::Selectable(std::string(romInfo.name).c_str(), selected)) {
				interacted = true;
				romType = static_cast<RomType>(count);
			}
			simpleToolTip(romInfo.description);
			++count;
		}
	});
	interacted |= ImGui::IsItemActive();
	return interacted;
}

bool ImGuiMedia::selectPatches(MediaItem& item, int& patchIndex)
{
	bool interacted = false;
	std::string patchesTitle = "IPS patches";
	if (!item.ipsPatches.empty()) {
		strAppend(patchesTitle, " (", item.ipsPatches.size(), ')');
	}
	strAppend(patchesTitle, "###patches");
	im::TreeNode(patchesTitle.c_str(), [&]{
		const auto& style = ImGui::GetStyle();
		auto width = style.ItemSpacing.x + 2.0f * style.FramePadding.x + ImGui::CalcTextSize("Remove"sv).x;
		ImGui::SetNextItemWidth(-width);
		im::Group([&]{
			im::ListBox("##", [&]{
				int count = 0;
				for (const auto& patch : item.ipsPatches) {
					auto preview = leftClip(patch, ImGui::GetContentRegionAvail().x);
					if (ImGui::Selectable(strCat(preview, "##", count).c_str(), count == patchIndex)) {
						interacted = true;
						patchIndex = count;
					}
					++count;
				}
			});
		});
		ImGui::SameLine();
		im::Group([&]{
			if (ImGui::Button("Add")) {
				interacted = true;
				manager.openFile->selectFile(
					"Select disk IPS patch",
					buildFilter("IPS patches", std::array<std::string_view, 1>{"ips"}),
					[&](const std::string& ips) {
						patchIndex = narrow<int>(item.ipsPatches.size());
						item.ipsPatches.push_back(ips);
					});
			}
			auto size = narrow<int>(item.ipsPatches.size());
			im::Disabled(patchIndex < 0 || patchIndex >= size, [&] {
				if (ImGui::Button("Remove")) {
					interacted = true;
					item.ipsPatches.erase(item.ipsPatches.begin() + patchIndex);
				}
				im::Disabled(patchIndex == 0, [&]{
					if (ImGui::ArrowButton("up", ImGuiDir_Up)) {
						std::swap(item.ipsPatches[patchIndex], item.ipsPatches[patchIndex - 1]);
						--patchIndex;
					}
				});
				im::Disabled(patchIndex == (size - 1), [&]{
					if (ImGui::ArrowButton("down", ImGuiDir_Down)) {
						std::swap(item.ipsPatches[patchIndex], item.ipsPatches[patchIndex + 1]);
						++patchIndex;
					}
				});
			});
		});
	});
	return interacted;
}

bool ImGuiMedia::insertMediaButton(std::string_view mediaName, ItemGroup& group, bool* showWindow)
{
	bool clicked = false;
	im::Disabled(group.edit.name.empty(), [&]{
		const auto& style = ImGui::GetStyle();
		auto width = 4.0f * style.FramePadding.x + style.ItemSpacing.x +
			     ImGui::CalcTextSize("Apply"sv).x + ImGui::CalcTextSize("Ok"sv).x;
		ImGui::SetCursorPosX(ImGui::GetContentRegionAvail().x - width + style.WindowPadding.x);
		clicked |= ImGui::Button("Apply");
		ImGui::SameLine();
		if (ImGui::Button("Ok")) {
			*showWindow = false;
			clicked = true;
		}
		if (clicked) {
			insertMedia(mediaName, group);
		}
	});
	return clicked;
}

TclObject ImGuiMedia::showDiskInfo(std::string_view mediaName, DiskMediaInfo& info)
{
	TclObject currentTarget;
	auto cmdResult = manager.execute(makeTclList("machine_info", "media", mediaName));
	if (!cmdResult) return currentTarget;

	int selectType = [&]{
		auto type = cmdResult->getOptionalDictValue(TclObject("type"));
		assert(type);
		auto s = type->getString();
		if (s == "empty") {
			return SELECT_EMPTY_DISK;
		} else if (s == "ramdsk") {
			return SELECT_RAMDISK;
		} else if (s == "dirasdisk") {
			return SELECT_DIR_AS_DISK;
		} else {
			assert(s == "file");
			return SELECT_DISK_IMAGE;
		}
	}();
	std::string_view typeStr = [&]{
		switch (selectType) {
			case SELECT_EMPTY_DISK:  return "No disk inserted";
			case SELECT_RAMDISK:     return "RAM disk";
			case SELECT_DIR_AS_DISK: return "Dir as disk:";
			case SELECT_DISK_IMAGE:  return "Disk image:";
			default: UNREACHABLE;
		}
	}();
	bool disableEject = selectType == SELECT_EMPTY_DISK;
	bool detailedInfo = selectType == one_of(SELECT_DIR_AS_DISK, SELECT_DISK_IMAGE);
	auto currentPatches = getPatches(*cmdResult);

	bool copyCurrent = false;
	im::Disabled(disableEject, [&]{
		copyCurrent = ImGui::SmallButton("Current disk");
		HelpMarker("Press to copy current disk to 'Select new disk' section.");
	});

	im::Indent([&]{
		ImGui::TextUnformatted(typeStr);
		if (detailedInfo) {
			if (auto target = cmdResult->getOptionalDictValue(TclObject("target"))) {
				currentTarget = *target;
				ImGui::SameLine();
				ImGui::TextUnformatted(leftClip(currentTarget.getString(),
				                       ImGui::GetContentRegionAvail().x));
			}
			std::string statusLine;
			auto add = [&](std::string_view s) {
				if (statusLine.empty()) {
					statusLine = s;
				} else {
					strAppend(statusLine, ", ", s);
				}
			};
			if (auto ro = cmdResult->getOptionalDictValue(TclObject("readonly"))) {
				if (ro->getOptionalBool().value_or(false)) {
					add("read-only");
				}
			}
			if (auto doubleSided = cmdResult->getOptionalDictValue(TclObject("doublesided"))) {
				add(doubleSided->getOptionalBool().value_or(true) ? "double-sided" : "single-sided");
			}
			if (auto size = cmdResult->getOptionalDictValue(TclObject("size"))) {
				add(tmpStrCat(size->getOptionalInt().value_or(0) / 1024, "kB"));
			}
			if (!statusLine.empty()) {
				ImGui::TextUnformatted(statusLine);
			}
			printPatches(currentPatches);
		}
	});
	if (copyCurrent && selectType != SELECT_EMPTY_DISK) {
		info.select = selectType;
		auto& edit = info.groups[selectType].edit;
		edit.name = currentTarget.getString();
		edit.ipsPatches = to_vector<std::string>(currentPatches);
	}
	im::Disabled(disableEject, [&]{
		if (ImGui::Button("Eject")) {
			manager.executeDelayed(makeTclList(mediaName, "eject"));
		}
	});
	ImGui::Separator();
	return currentTarget;
}

void ImGuiMedia::printDatabase(const RomInfo& romInfo, const char* buf)
{
	auto printRow = [](std::string_view description, std::string_view value) {
		if (value.empty()) return;
		if (ImGui::TableNextColumn()) {
			ImGui::TextUnformatted(description);
		}
		if (ImGui::TableNextColumn()) {
			ImGui::TextUnformatted(value);
		}
	};

	printRow("Title",   romInfo.getTitle(buf));
	printRow("Year",    romInfo.getYear(buf));
	printRow("Company", romInfo.getCompany(buf));
	printRow("Country", romInfo.getCountry(buf));
	auto status = [&]{
		auto str = romInfo.getOrigType(buf);
		if (romInfo.getOriginal()) {
			std::string result = "Unmodified dump";
			if (!str.empty()) {
				strAppend(result, " (confirmed by ", str, ')');
			}
			return result;
		} else {
			return std::string(str);
		}
	}();
	printRow("Status", status);
	printRow("Remark", romInfo.getRemark(buf));
}

static void printRomInfo(ImGuiManager& manager, const TclObject& mediaTopic, std::string_view filename, RomType romType)
{
	im::Table("##extension-info", 2, [&]{
		ImGui::TableSetupColumn("description", ImGuiTableColumnFlags_WidthFixed);
		ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch);

		if (ImGui::TableNextColumn()) {
			ImGui::TextUnformatted("Filename"sv);
		}
		if (ImGui::TableNextColumn()) {
			ImGui::TextUnformatted(leftClip(filename, ImGui::GetContentRegionAvail().x));
		}

		auto& database = manager.getReactor().getSoftwareDatabase();
		const auto* romInfo = [&]() -> const RomInfo* {
			if (auto actual = mediaTopic.getOptionalDictValue(TclObject("actualSHA1"))) {
				if (const auto* info = database.fetchRomInfo(Sha1Sum(actual->getString()))) {
					return info;
				}
			}
			if (auto original = mediaTopic.getOptionalDictValue(TclObject("originalSHA1"))) {
				if (const auto* info = database.fetchRomInfo(Sha1Sum(original->getString()))) {
					return info;
				}
			}
			return nullptr;
		}();
		if (romInfo) {
			ImGuiMedia::printDatabase(*romInfo, database.getBufferStart());
		}

		std::string mapperStr{RomInfo::romTypeToName(romType)};
		if (romInfo) {
			if (auto dbType = romInfo->getRomType();
			dbType != ROM_UNKNOWN && dbType != romType) {
				strAppend(mapperStr, " (database: ", RomInfo::romTypeToName(dbType), ')');
			}
		}
		if (ImGui::TableNextColumn()) {
			ImGui::TextUnformatted("Mapper"sv);
		}
		if (ImGui::TableNextColumn()) {
			ImGui::TextUnformatted(mapperStr);
		}
	});
}

TclObject ImGuiMedia::showCartridgeInfo(std::string_view mediaName, CartridgeMediaInfo& info, int slot)
{
	TclObject currentTarget;
	auto cmdResult = manager.execute(makeTclList("machine_info", "media", mediaName));
	if (!cmdResult) return currentTarget;

	int selectType = [&]{
		if (auto type = cmdResult->getOptionalDictValue(TclObject("type"))) {
			auto s = type->getString();
			if (s == "extension") {
				return SELECT_EXTENSION;
			} else {
				assert(s == "rom");
				return SELECT_ROM_IMAGE;
			}
		} else {
			return SELECT_EMPTY_SLOT;
		}
	}();
	bool disableEject = selectType == SELECT_EMPTY_SLOT;
	auto currentPatches = getPatches(*cmdResult);

	bool copyCurrent = false;
	im::Disabled(disableEject, [&]{
		copyCurrent = ImGui::SmallButton("Current cartridge");
	});
	auto& slotManager = manager.getReactor().getMotherBoard()->getSlotManager();
	ImGui::SameLine();
	ImGui::TextUnformatted(tmpStrCat("(slot ", slotManager.getPsSsString(slot), ')'));

	RomType currentRomType = ROM_UNKNOWN;
	im::Indent([&]{
		if (selectType == SELECT_EMPTY_SLOT) {
			ImGui::TextUnformatted("No cartridge inserted"sv);
		} else if (auto target = cmdResult->getOptionalDictValue(TclObject("target"))) {
			currentTarget = *target;
			if (selectType == SELECT_EXTENSION) {
				if (auto* i = findExtensionInfo(target->getString())) {
					printExtensionInfo(*i);
				}
			} else if (selectType == SELECT_ROM_IMAGE) {
				if (auto mapper = cmdResult->getOptionalDictValue(TclObject("mappertype"))) {
					currentRomType = RomInfo::nameToRomType(mapper->getString());
				}
				printRomInfo(manager, *cmdResult, target->getString(), currentRomType);
				printPatches(currentPatches);
			}
		}
	});
	if (copyCurrent && selectType != SELECT_EMPTY_SLOT) {
		info.select = selectType;
		auto& edit = info.groups[selectType].edit;
		edit.name = currentTarget.getString();
		edit.ipsPatches = to_vector<std::string>(currentPatches);
		edit.romType = currentRomType;
	}
	im::Disabled(disableEject, [&]{
		if (ImGui::Button("Eject")) {
			manager.executeDelayed(makeTclList(mediaName, "eject"));
		}
	});
	ImGui::Separator();
	return currentTarget;
}

void ImGuiMedia::diskMenu(int i)
{
	auto& info = diskMediaInfo[i];
	auto mediaName = strCat("disk", char('a' + i));
	auto displayName = strCat("Disk Drive ", char('A' + i));
	ImGui::SetNextWindowSize(gl::vec2{29, 22} * ImGui::GetFontSize(), ImGuiCond_FirstUseEver);
	im::Window(displayName.c_str(), &info.show, [&]{
		auto current = showDiskInfo(mediaName, info);
		im::Child("select", {0, -ImGui::GetFrameHeightWithSpacing()}, [&]{
			ImGui::TextUnformatted("Select new disk"sv);

			ImGui::RadioButton("disk image", &info.select, SELECT_DISK_IMAGE);
			im::VisuallyDisabled(info.select != SELECT_DISK_IMAGE, [&]{
				im::Indent([&]{
					auto& group = info.groups[SELECT_DISK_IMAGE];
					auto createNew = [&]{
						manager.openFile->selectNewFile(
							"Select name for new blank disk image",
							"Disk images (*.dsk){.dsk}",
							[&](const auto& fn) {
								group.edit.name = fn;
								auto& diskManipulator = manager.getReactor().getDiskManipulator();
								try {
									diskManipulator.create(fn, MSXBootSectorType::DOS2, {1440});
								} catch (MSXException& e) {
									manager.printError("Couldn't create new disk image: ", e.getMessage());
								}
							},
							current.getString());
					};
					bool interacted = selectImage(
						group, strCat("Select disk image for ", displayName), &diskFilter,
						current.getString(), std::identity{}, createNew);
					interacted |= selectPatches(group.edit, group.patchIndex);
					if (interacted) info.select = SELECT_DISK_IMAGE;
				});
			});
			ImGui::RadioButton("dir as disk", &info.select, SELECT_DIR_AS_DISK);
			im::VisuallyDisabled(info.select != SELECT_DIR_AS_DISK, [&]{
				im::Indent([&]{
					auto& group = info.groups[SELECT_DIR_AS_DISK];
					auto createNew = [&]{
						manager.openFile->selectNewFile(
							"Select name for new empty directory",
							"",
							[&](const auto& fn) {
								group.edit.name = fn;
								try {
									FileOperations::mkdirp(fn);
								} catch (MSXException& e) {
									manager.printError("Couldn't create directory: ", e.getMessage());
								}
							},
							current.getString());
					};
					bool interacted = selectDirectory(
						group, strCat("Select directory for ", displayName),
						current.getString(), createNew);
					if (interacted) info.select = SELECT_DIR_AS_DISK;
				});
			});
			ImGui::RadioButton("RAM disk", &info.select, SELECT_RAMDISK);
		});
		insertMediaButton(mediaName, info.groups[info.select], &info.show);
	});
}

void ImGuiMedia::cartridgeMenu(int cartNum)
{
	auto& info = cartridgeMediaInfo[cartNum];
	auto displayName = strCat("Cartridge Slot ", char('A' + cartNum));
	ImGui::SetNextWindowSize(gl::vec2{37, 30} * ImGui::GetFontSize(), ImGuiCond_FirstUseEver);
	im::Window(displayName.c_str(), &info.show, [&]{
		auto cartName = strCat("cart", char('a' + cartNum));
		auto extName = strCat("ext", char('a' + cartNum));

		auto current = showCartridgeInfo(cartName, info, cartNum);

		im::Child("select", {0, -ImGui::GetFrameHeightWithSpacing()}, [&]{
			ImGui::TextUnformatted("Select new cartridge:"sv);

			ImGui::RadioButton("ROM image", &info.select, SELECT_ROM_IMAGE);
			im::VisuallyDisabled(info.select != SELECT_ROM_IMAGE, [&]{
				im::Indent([&]{
					auto& group = info.groups[SELECT_ROM_IMAGE];
					auto& item = group.edit;
					bool interacted = selectImage(
						group, strCat("Select ROM image for ", displayName), &romFilter, current.getString());
						//[&](const std::string& filename) { return displayNameForRom(filename); }); // not needed?
					const auto& style = ImGui::GetStyle();
					ImGui::SetNextItemWidth(-(ImGui::CalcTextSize("mapper-type").x + style.ItemInnerSpacing.x));
					interacted |= selectMapperType("mapper-type", item.romType);
					interacted |= selectPatches(item, group.patchIndex);
					interacted |= ImGui::Checkbox("Reset MSX on inserting ROM", &resetOnInsertRom);
					if (interacted) info.select = SELECT_ROM_IMAGE;
				});
			});
			ImGui::RadioButton("extension", &info.select, SELECT_EXTENSION);
			im::VisuallyDisabled(info.select != SELECT_EXTENSION, [&]{
				im::Indent([&]{
					auto& allExtensions = getAllExtensions();
					auto& group = info.groups[SELECT_EXTENSION];
					auto& item = group.edit;

					bool interacted = drawExtensionFilter();

					auto drawExtensions = [&]{
						auto filteredExtensions = to_vector(xrange(allExtensions.size()));
						applyComboFilter("Type", filterType, allExtensions, filteredExtensions);
						applyDisplayNameFilter(filterString, allExtensions, filteredExtensions);

						im::ListClipper(filteredExtensions.size(), [&](int i) {
							auto& ext = allExtensions[filteredExtensions[i]];
							bool ok = getTestResult(ext).empty();
							im::StyleColor(!ok, ImGuiCol_Text, getColor(imColor::ERROR), [&]{
								if (ImGui::Selectable(ext.displayName.c_str(), item.name == ext.configName)) {
									interacted = true;
									item.name = ext.configName;
								}
								if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
									insertMedia(extName, group); // Apply
								}
								extensionTooltip(ext);
							});
						});
					};
					if (filterOpen) {
						im::ListBox("##list", [&]{
							drawExtensions();
						});
					} else {
						im::Combo("##extension", displayNameForExtension(item.name).c_str(), [&]{
							drawExtensions();
						});
					}

					interacted |= ImGui::IsItemActive();
					if (interacted) info.select = SELECT_EXTENSION;
				});
			});
		});
		if (insertMediaButton(info.select == SELECT_ROM_IMAGE ? cartName : extName,
		                      info.groups[info.select], &info.show)) {
			if (resetOnInsertRom && info.select == SELECT_ROM_IMAGE) {
				manager.executeDelayed(TclObject("reset"));
			}
		}
	});
}

static void addRecent(ImGuiMedia::ItemGroup& group)
{
	auto& recent = group.recent;
	if (auto it2 = ranges::find(recent, group.edit); it2 != recent.end()) {
		// was already present, move to front
		std::rotate(recent.begin(), it2, it2 + 1);
	} else {
		// new entry, add it, but possibly remove oldest entry
		if (recent.full()) recent.pop_back();
		recent.push_front(group.edit);
	}
}

static bool ButtonWithCustomRendering(
	const char* label, gl::vec2 size, bool pressed,
	std::invocable<gl::vec2 /*center*/, ImDrawList*> auto render)
{
	bool result = false;
	im::StyleColor(pressed, ImGuiCol_Button, ImGui::GetColorU32(ImGuiCol_ButtonActive), [&]{
		gl::vec2 topLeft = ImGui::GetCursorScreenPos();
		gl::vec2 center = topLeft + size * 0.5f;
		result = ImGui::Button(label, size);
		render(center, ImGui::GetWindowDrawList());
	});
	return result;
}

static void RenderPlay(gl::vec2 center, ImDrawList* drawList)
{
	float half = 0.4f * ImGui::GetTextLineHeight();
	auto p1 = center + gl::vec2(half, 0.0f);
	auto p2 = center + gl::vec2(-half, half);
	auto p3 = center + gl::vec2(-half, -half);
	drawList->AddTriangleFilled(p1, p2, p3, getColor(imColor::TEXT));
}
static void RenderRewind(gl::vec2 center, ImDrawList* drawList)
{
	float size = 0.8f * ImGui::GetTextLineHeight();
	float half = size * 0.5f;
	auto color = getColor(imColor::TEXT);
	auto p1 = center + gl::vec2(-size, 0.0f);
	auto p2 = center + gl::vec2(0.0f, -half);
	auto p3 = center + gl::vec2(0.0f, half);
	drawList->AddTriangleFilled(p1, p2, p3, color);
	gl::vec2 offset{size, 0.0f};
	p1 += offset;
	p2 += offset;
	p3 += offset;
	drawList->AddTriangleFilled(p1, p2, p3, color);
}
static void RenderStop(gl::vec2 center, ImDrawList* drawList)
{
	gl::vec2 half{0.4f * ImGui::GetTextLineHeight()};
	drawList->AddRectFilled(center - half, center + half, getColor(imColor::TEXT));
}
static void RenderRecord(gl::vec2 center, ImDrawList* drawList)
{
	float radius = 0.4f * ImGui::GetTextLineHeight();
	drawList->AddCircleFilled(center, radius, getColor(imColor::TEXT));
}


void ImGuiMedia::cassetteMenu(const TclObject& cmdResult)
{
	ImGui::SetNextWindowSize(gl::vec2{29, 20} * ImGui::GetFontSize(), ImGuiCond_FirstUseEver);
	auto& info = cassetteMediaInfo;
	auto& group = info.group;
	im::Window("Tape Deck", &info.show, [&]{
		ImGui::TextUnformatted("Current tape"sv);
		auto current = cmdResult.getListIndexUnchecked(1).getString();
		im::Indent([&]{
			if (current.empty()) {
				ImGui::TextUnformatted("No tape inserted"sv);
			} else {
				ImGui::TextUnformatted("Tape image:"sv);
				ImGui::SameLine();
				ImGui::TextUnformatted(leftClip(current, ImGui::GetContentRegionAvail().x));
			}
		});
		im::Disabled(current.empty(), [&]{
			if (ImGui::Button("Eject")) {
				manager.executeDelayed(makeTclList("cassetteplayer", "eject"));
			}
		});
		ImGui::Separator();

		ImGui::TextUnformatted("Controls"sv);
		im::Indent([&]{
			auto status = cmdResult.getListIndexUnchecked(2).getString();
			auto size = ImGui::GetFrameHeightWithSpacing();
			if (ButtonWithCustomRendering("##Play", {2.0f * size, size}, status == "play", RenderPlay)) {
				manager.executeDelayed(makeTclList("cassetteplayer", "play"));
			}
			ImGui::SameLine();
			if (ButtonWithCustomRendering("##Rewind", {2.0f * size, size}, false, RenderRewind)) {
				manager.executeDelayed(makeTclList("cassetteplayer", "rewind"));
			}
			ImGui::SameLine();
			if (ButtonWithCustomRendering("##Stop", {2.0f * size, size}, status == "stop", RenderStop)) {
				// nothing, this button only exists to indicate stop-state
			}
			ImGui::SameLine();
			if (ButtonWithCustomRendering("##Record", {2.0f * size, size}, status == "record", RenderRecord)) {
				manager.openFile->selectNewFile(
					"Select new wav file for record",
					"Tape images (*.wav){.wav}",
					[&](const auto& fn) {
						group.edit.name = fn;
						manager.executeDelayed(makeTclList("cassetteplayer", "new", fn),
							[&group](const TclObject&) {
								// only add to 'recent' when command succeeded
								addRecent(group);
							});
					},
					current);
			}

			ImGui::SameLine();
			auto getFloat = [&](std::string_view subCmd) {
				auto r = manager.execute(makeTclList("cassetteplayer", subCmd)).value_or(TclObject(0.0));
				return r.getOptionalFloat().value_or(0.0f);
			};
			auto length = getFloat("getlength");
			auto pos = getFloat("getpos");
			auto format = [](float time) {
				int t = narrow_cast<int>(time); // truncated to seconds
				int s = t % 60; t /= 60;
				int m = t % 60; t /= 60;
				std::ostringstream os;
				os << std::setfill('0');
				if (t) os << std::setw(2) << t << ':';
				os << std::setw(2) << m << ':';
				os << std::setw(2) << s;
				return os.str();
			};
			ImGui::Text("%s / %s", format(pos).c_str(), format(length).c_str());

			auto& reactor = manager.getReactor();
			auto& controller = reactor.getMotherBoard()->getMSXCommandController();
			const auto& hotKey = reactor.getHotKey();
			if (auto* autoRun = dynamic_cast<BooleanSetting*>(controller.findSetting("autoruncassettes"))) {
				Checkbox(hotKey, "(try to) Auto Run", *autoRun);
			}
			if (auto* mute = dynamic_cast<BooleanSetting*>(controller.findSetting("cassetteplayer_ch1_mute"))) {
				Checkbox(hotKey, "Mute tape audio", *mute, [](const Setting&) { return std::string{}; });
			}
		});
		ImGui::Separator();

		im::Child("select", {0, -ImGui::GetFrameHeightWithSpacing()}, [&]{
			ImGui::TextUnformatted("Select new tape:"sv);
			im::Indent([&]{
				selectImage(group, "Select tape image", &cassetteFilter, current);
			});
		});
		insertMediaButton("cassetteplayer", group, &info.show);
	});
}

void ImGuiMedia::insertMedia(std::string_view mediaName, ItemGroup& group)
{
	auto& item = group.edit;
	if (item.name.empty()) return;

	auto cmd = makeTclList(mediaName, "insert", item.name);
	for (const auto& patch : item.ipsPatches) {
		cmd.addListElement("-ips", patch);
	}
	if (item.romType != ROM_UNKNOWN) {
		cmd.addListElement("-romtype", RomInfo::romTypeToName(item.romType));
	}
	manager.executeDelayed(cmd,
		[&group](const TclObject&) {
			// only add to 'recent' when insert command succeeded
			addRecent(group);
		});
}

} // namespace openmsx
