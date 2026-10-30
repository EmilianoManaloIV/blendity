// SPDX-License-Identifier: GPL-2.0-or-later
// Blendity Live Link for Unreal 5.3+ (task 0277): an editor module with a dock tab (Window > Blendity Live Link) that
// shows the state, Auto Connect, a port and Connect / Disconnect; the client is ticked by the core ticker.
#include "BlendityLinkClient.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Docking/TabManager.h"
#include "Misc/ConfigCacheIni.h"
#include "Modules/ModuleManager.h"
#include "Widgets/Docking/SDockTab.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "BlendityLiveLink"

namespace
{
const FName TabName(TEXT("BlendityLiveLink"));
const TCHAR* Section = TEXT("BlendityLiveLink");
}

class FBlendityLiveLinkModule : public IModuleInterface
{
public:
	void StartupModule() override
	{
		GConfig->GetBool(Section, TEXT("AutoConnect"), Client.bAutoConnect, GEditorPerProjectIni);
		GConfig->GetInt(Section, TEXT("Port"), Client.ManualPort, GEditorPerProjectIni);
		TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FBlendityLiveLinkModule::Tick), 0.0f);
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(TabName, FOnSpawnTab::CreateRaw(this, &FBlendityLiveLinkModule::SpawnTab))
			.SetDisplayName(LOCTEXT("Title", "Blendity Live Link"))
			.SetTooltipText(LOCTEXT("Tip", "Follow the scene open in Blendity live"))
			.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory());
	}

	void ShutdownModule() override
	{
		FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
		if (FSlateApplication::IsInitialized())
			FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(TabName);
		Client.Disconnect();
	}

private:
	bool Tick(float)
	{
		Client.Tick();
		return true;
	}

	void Save()
	{
		GConfig->SetBool(Section, TEXT("AutoConnect"), Client.bAutoConnect, GEditorPerProjectIni);
		GConfig->SetInt(Section, TEXT("Port"), Client.ManualPort, GEditorPerProjectIni);
	}

	FText CountsText() const
	{
		const FBlendityLinkApplier* A = Client.GetApplier();
		if (!A)
			return FText::GetEmpty();
		return FText::FromString(FString::Printf(TEXT("%d messages, %d objects%s"), Client.Messages, A->ObjectCount(), A->IsPlaying() ? TEXT(" - Blendity is in Play mode") : TEXT("")));
	}

	TSharedRef<SDockTab> SpawnTab(const FSpawnTabArgs&)
	{
		return SNew(SDockTab).TabRole(ETabRole::NomadTab)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([this] { return Client.bAutoConnect ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { Client.bAutoConnect = S == ECheckBoxState::Checked; Save(); })
				.ToolTipText(LOCTEXT("AutoTip", "Connect by itself whenever Blendity's Live Link runs (it writes ~/.blendity/livelink.json)."))
				[SNew(STextBlock).Text(LOCTEXT("Auto", "Auto Connect"))]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)[SNew(STextBlock).Text(LOCTEXT("Port", "Port"))]
				+ SHorizontalBox::Slot().FillWidth(1)
				[
					SNew(SSpinBox<int32>).MinValue(0).MaxValue(65535)
					.Value_Lambda([this] { return Client.ManualPort; })
					.OnValueCommitted_Lambda([this](int32 V, ETextCommit::Type) { Client.ManualPort = V; Save(); })
					.ToolTipText(LOCTEXT("PortTip", "0: the port Blendity's discovery file names. Another: always this one."))
				]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)
			[
				SNew(SButton)
				.Text_Lambda([this] { return Client.HasSocket() ? LOCTEXT("Disconnect", "Disconnect") : LOCTEXT("Connect", "Connect"); })
				.OnClicked_Lambda([this]
				{
					if (Client.HasSocket())
						Client.Hold();
					else
					{
						Client.bHeld = false;
						if (!Client.TryAutoConnect())
							Client.ConnectTo(40100);
					}
					return FReply::Handled();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).AutoWrapText(true).Text_Lambda([this] { return FText::FromString(Client.Status); })]
			+ SVerticalBox::Slot().AutoHeight().Padding(6)[SNew(STextBlock).Text_Lambda([this] { return CountsText(); })]
		];
	}

	FBlendityLinkClient Client;
	FTSTicker::FDelegateHandle TickHandle;
};

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FBlendityLiveLinkModule, BlendityLiveLink)
