#include "UBrowsePrivatePCH.h"

#include "UnrealEd.h"
#include "SourceCodeNavigation.h"
#include "SlateBasics.h"
#include "SlateExtras.h"

#include "UBrowseStyle.h"
#include "UBrowseCommands.h"

#include "IDetailsView.h"
#include "PropertyEditing.h"
#include "EditorFontGlyphs.h"

#include "ClassViewerModule.h"
#include "SClassPickerDialog.h"
#include "PropertyEditorModule.h"
#include "LevelEditor.h"
#include "SUBrowserTableRow.h"
#include "SUBrowsePropertyTableRow.h"
#include "SUBrowserClassItem.h"
#include "SUBrowsePanel.h"
#include "SUBrowser.h"

#define LOCTEXT_NAMESPACE "SUBrowserMenu"

BEGIN_SLATE_FUNCTION_BUILD_OPTIMIZATION
void SUBrowser::Construct(const FArguments& InArgs)
{
	bShouldIncludeClassDefaultObjects = false; 
	bShouldIncludeDefaultSubObjects = false;
	bShouldIncludeArchetypeObjects = false;
	bOnlyListRootObjects = false;
	bIncludeTransient = false;
	bOnlyListGCObjects = false;

	Tag = FName(TEXT("UBrowseTag"));
	SortBy = EQuerySortMode::ByID;
	SortDirection = EColumnSortMode::Descending;
	FilterClass = UBlueprint::StaticClass();
	FText WidgetText = FText::Format(
		LOCTEXT("WindowWidgetText", "Add code to {0} in {1} to override this window's contents"),
		FText::FromString(TEXT("FUBrowseModule::OnSpawnPluginTab")),
		FText::FromString(TEXT("UBrowse.cpp"))
	);
	// Create a property view
	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs(
		/*bUpdateFromSelection=*/ false,
		/*bLockable=*/ false,
		/*bAllowSearch=*/ true,
		FDetailsViewArgs::ObjectsUseNameArea,
		/*bHideSelectionTip=*/ true,
		/*InNotifyHook=*/ nullptr,
		/*InSearchInitialKeyFocus=*/ false,
		/*InViewIdentifier=*/ FName(TEXT("UBrowse")));
	DetailsViewArgs.DefaultsOnlyVisibility = FDetailsViewArgs::EEditDefaultsOnlyNodeVisibility::Automatic;
	TSharedPtr<FUBrowserPanel> FirstBrowserPanel(new FUBrowserPanel);
	TSharedPtr<SUBrowsePanel> FirstSUBrowsePanel = SNew(SUBrowsePanel).OnNodeDoubleClicked(this, &SUBrowser::OnNodeDoubleClicked);
	FirstBrowserPanel->BrowsePanel = FirstSUBrowsePanel;
	BrowserPanels.Add(FirstBrowserPanel);
	OnNewObjectView = FirstSUBrowsePanel->OnNewObjectView;
	PropertyView = EditModule.CreateDetailView(DetailsViewArgs);
	FOnGetDetailCustomizationInstance UBrowseLiteralDetails = FOnGetDetailCustomizationInstance::CreateStatic(&FBrowserObject::MakeInstance);
	PropertyView->RegisterInstancedCustomPropertyLayout(UObject::StaticClass(), UBrowseLiteralDetails);
	ChildSlot
		[
			SNew(SSplitter)
			.Orientation(EOrientation::Orient_Horizontal)
		/* Object / Class selection list */
		+ SSplitter::Slot()
		.Value(3)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			/* Top bar of object viewer */
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.OnClicked(this, &SUBrowser::OnClassSelectionClicked)
		[
			SNew(STextBlock)
			.Text(this, &SUBrowser::GetFilterClassText)
		.ToolTipText(LOCTEXT("ClassName", "Class Filter"))
		]
		]
	+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SEditableTextBox)
			.HintText(LOCTEXT("ObjectName", "Object Name Filter"))
		.OnTextCommitted(this, &SUBrowser::OnNewHostTextCommited)
		.OnTextChanged(this, &SUBrowser::OnNewHostTextCommited, ETextCommit::Default)
		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SComboButton)
			.ComboButtonStyle(FEditorStyle::Get(), "ContentBrowser.Filters.Style")
		.ForegroundColor(FLinearColor::White)
		.ContentPadding(0)
		.ToolTipText(LOCTEXT("AddFilterToolTip", "Add a search filter."))
		.OnGetMenuContent(this, &SUBrowser::MakeFilterMenu)
		.HasDownArrow(true)
		.ContentPadding(FMargin(1, 0))
		.ButtonContent()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "ContentBrowser.Filters.Text")
		.Font(FEditorStyle::Get().GetFontStyle("FontAwesome.9"))
		.Text(FEditorFontGlyphs::Filter)
		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(2, 0, 0, 0)
		[
			SNew(STextBlock)
			.TextStyle(FEditorStyle::Get(), "ContentBrowser.Filters.Text")
		.Text(LOCTEXT("Filters", "Filters"))
		]
		]
		]
	+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SButton)
			.OnClicked(this, &SUBrowser::OnCollectGarbage)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("CollectGarbage", "Collect Garbage"))
		]
		]
		]
	/* The actual list of objects */
	+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(0.0f, 4.0f))
		[
			SAssignNew(ObjectListView, SListView< TSharedPtr<FBrowserObject> >)
			.ItemHeight(24.0f)
		.ListItemsSource(&(this->GetLiveObjects()))
		.SelectionMode(ESelectionMode::Single)
		.OnGenerateRow(this, &SUBrowser::OnGenerateObjectListRow)
		.OnSelectionChanged(this, &SUBrowser::OnObjectListSelectionChanged)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+ SHeaderRow::Column("Name")
			.OnSort(this, &SUBrowser::OnSortByChanged)
			.SortMode(this, &SUBrowser::GetSortMode)
			.DefaultLabel(LOCTEXT("SUBrowserNameCol", "Name"))
			.DefaultTooltip(LOCTEXT("SUBrowserNameColTooltip", "Object Name"))
			.FillWidth(0.2f)
			.HAlignCell(HAlign_Left)
			.HAlignHeader(HAlign_Left)
			.VAlignCell(VAlign_Center)
			+ SHeaderRow::Column("Number")
			.OnSort(this, &SUBrowser::OnSortByChanged)
			.SortMode(this, &SUBrowser::GetSortMode)
			.DefaultLabel(LOCTEXT("SUBrowserNumberCol", "Number"))
			.DefaultTooltip(LOCTEXT("SUBrowserNumberColTooltip", "Object Number"))
			.FillWidth(0.2f)
			.HAlignCell(HAlign_Center)
			.HAlignHeader(HAlign_Center)
			.VAlignCell(VAlign_Center)
			+ SHeaderRow::Column("Class")
			.OnSort(this, &SUBrowser::OnSortByChanged)
			.SortMode(this, &SUBrowser::GetSortMode)
			.DefaultLabel(LOCTEXT("SUBrowserClassCol", "Class"))
			.DefaultTooltip(LOCTEXT("SUBrowserClassColTooltip", "Object Class"))
			.FillWidth(0.2f)
			.HAlignCell(HAlign_Left)
			.HAlignHeader(HAlign_Left)
			.VAlignCell(VAlign_Center)
		)
		]
		]
	/* History Viewer */
	+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SBorder)
			.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		.Padding(FMargin(0.0f, 4.0f))
		[
			SAssignNew(ObjectHistoryView, SListView< TSharedPtr< FBrowserObject > >)
			.ItemHeight(24)
		.ListItemsSource(&(this->GetCurrentHistoryList()))
		// Currently, we only need single-selection for this tree
		.SelectionMode(ESelectionMode::Single)
		.ClearSelectionOnClick(false)
		.OnGenerateRow(this, &SUBrowser::OnGenerateHistoryRow)
		.OnSelectionChanged(this, &SUBrowser::OnHistorySelectionChanged)
		.HeaderRow
		(
			SNew(SHeaderRow)
			+ SHeaderRow::Column("Name").DefaultLabel(LOCTEXT("SUBrowseObjectName", "Name"))
			+ SHeaderRow::Column("Number").DefaultLabel(LOCTEXT("SUBrowseObjectNumber", "Number"))
			+ SHeaderRow::Column("Class").DefaultLabel(LOCTEXT("SUBrowseObjectClass", "Class"))
			+ SHeaderRow::Column("Id").DefaultLabel(LOCTEXT("SUBrowseObjectId", "Id"))
		)
		]
		]
		]
		]
	/* The actual browse panel */
	+ SSplitter::Slot()
		.Value(8)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
		.BorderImage(FEditorStyle::GetBrush("ToolPanel.GroupBorder"))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SAssignNew(UBrowseSwitcher, SWidgetSwitcher)
			.WidgetIndex(0)
		+ SWidgetSwitcher::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			FirstSUBrowsePanel.ToSharedRef()
		]
		]
		]
		]
		]
	+ SSplitter::Slot()
		.Value(2)
		[
			SNew(SBorder)
			.Padding(FMargin(3))
		[
			PropertyView.ToSharedRef()
		]
		]
		];
};


void SUBrowser::AddBoolFilter(FMenuBuilder& MenuBuilder, FText Text, FText MenuToolTip, bool* BoolOption)
{
	MenuBuilder.AddMenuEntry(
		Text,
		MenuToolTip,
		FSlateIcon(),
		FUIAction(
			FExecuteAction::CreateLambda([=] { *BoolOption = !(*BoolOption); RefreshList(); }),
			FCanExecuteAction(),
			FIsActionChecked::CreateLambda([=] { return *BoolOption; })
		),
		NAME_None,
		EUserInterfaceActionType::ToggleButton
	);
}

TSharedRef<SWidget> SUBrowser::MakeFilterMenu()
{
	const bool bInShouldCloseWindowAfterMenuSelection = true;
	FMenuBuilder MenuBuilder(bInShouldCloseWindowAfterMenuSelection, nullptr);

	MenuBuilder.BeginSection("AssetViewType", LOCTEXT("ViewTypeHeading", "View Type"));

	AddBoolFilter(
		MenuBuilder,
		LOCTEXT("ShouldIncludeClassDefaultObjects", "Include Default Objects (CDO)"),
		LOCTEXT("ShouldIncludeClassDefaultObjectsToolTip", "Should we include Class Default Objects in the results?"),
		&bShouldIncludeClassDefaultObjects);

	AddBoolFilter(
		MenuBuilder,
		LOCTEXT("ShouldIncludeDefaultSubObjects", "Include Default Sub Objects"),
		LOCTEXT("ShouldIncludeDefaultSubObjectsToolTip", "Should we include Default Sub Objects in the results?"),
		&bShouldIncludeDefaultSubObjects);

	AddBoolFilter(
		MenuBuilder,
		LOCTEXT("ShouldIncludeArchetypeObjects", "Include Archetyoe Objects"),
		LOCTEXT("ShouldIncludeArchetypeObjectsToolTip", "Should we include Archetype Objects in the results?"),
		&bShouldIncludeArchetypeObjects);

	AddBoolFilter(
		MenuBuilder,
		LOCTEXT("OnlyListRootObjects", "Only List Root Objects"),
		LOCTEXT("OnlyListRootObjectsToolTip", ""),
		&bOnlyListRootObjects);

	AddBoolFilter(
		MenuBuilder,
		LOCTEXT("OnlyListGCObjects", "Only List GC Objects"),
		LOCTEXT("OnlyListGCObjectsToolTip", ""),
		&bOnlyListGCObjects);

	AddBoolFilter(
		MenuBuilder,
		LOCTEXT("IncludeTransient", "Include Transient"),
		LOCTEXT("IncludeTransientToolTip", "Include objects in transient packages?"),
		&bIncludeTransient);

	return MenuBuilder.MakeWidget();
}
END_SLATE_FUNCTION_BUILD_OPTIMIZATION

FUBrowserPanel& SUBrowser::GetCurrentBrowserPanel()
{
	return *(BrowserPanels[0]);
}

const TArray< TSharedPtr<FBrowserObject> >& SUBrowser::GetLiveObjects()
{
	FUBrowserPanel& Panel(GetCurrentBrowserPanel());
	return Panel.LiveObjects;
}


const TArray< TSharedPtr<FBrowserObject> >& SUBrowser::GetCurrentHistoryList()
{
	return History;
}

void SUBrowser::RefreshList()
{
	/*
		UObject* CheckOuter = nullptr;
		UPackage* InsidePackage = nullptr;
	*/
	FUBrowserPanel& Panel(GetCurrentBrowserPanel());

	Panel.LiveObjects.Reset();

	EObjectFlags ExclusionFlags{ RF_NoFlags };

	if (!bShouldIncludeDefaultSubObjects)
	{
		ExclusionFlags |= RF_DefaultSubObject;
	}

	if (!bShouldIncludeArchetypeObjects)
	{
		ExclusionFlags |= RF_ArchetypeObject;
	}

	if (!bShouldIncludeClassDefaultObjects)
	{
		ExclusionFlags |= RF_ClassDefaultObject;
	}

	for (TObjectIterator<UObject> It(ExclusionFlags); It; ++It)
	{

		if (bOnlyListGCObjects && GUObjectArray.IsDisregardForGC(*It))
		{
			continue;
		}

		if (bOnlyListRootObjects && !It->IsRooted())
		{
			continue;
		}

		if (!bIncludeTransient)
		{
			UPackage* ContainerPackage = It->GetOutermost();
			if (ContainerPackage == GetTransientPackage() || ContainerPackage->HasAnyFlags(RF_Transient))
			{
				continue;
			}
		}

		if ((FilterClass != nullptr) && (!It->GetClass()->IsChildOf(FilterClass)))
		{
			continue;
		}


		if (!FilterString.IsEmpty() && !It->GetName().Contains(FilterString))
		{
			continue;
		}

		TSharedPtr<FBrowserObject> NewObject = MakeShareable(new FBrowserObject());
		NewObject->Object = *It;

		Panel.LiveObjects.Add(NewObject);
	}

	if (SortBy == EQuerySortMode::ByID) {
		struct FCompareObjectsByName
		{
			FORCEINLINE bool operator()(const TSharedPtr< FBrowserObject > A, const TSharedPtr< FBrowserObject > B) const
			{
				return GetNameSafe(A->Object.Get()) < GetNameSafe(B->Object.Get());
			}
		};
		Panel.LiveObjects.Sort(FCompareObjectsByName());
	}
	if (SortBy == EQuerySortMode::ByType)
	{
		struct FCompareObjectsByClass
		{
			FORCEINLINE bool operator()(const TSharedPtr< FBrowserObject > A, const TSharedPtr< FBrowserObject > B) const
			{
				return GetNameSafe(A->Object->GetClass()) < GetNameSafe(B->Object->GetClass());
			}
		};
		Panel.LiveObjects.Sort(FCompareObjectsByClass());
	}
	if (SortBy == EQuerySortMode::ByNumber)
	{
		struct FCommpareObjectsByNumber
		{
			FORCEINLINE bool operator()(const TSharedPtr< FBrowserObject > A, const TSharedPtr< FBrowserObject > B) const
			{
				int32 NumberA = A->Object.Get() ? A->Object.Get()->GetFName().GetNumber() : 0;
				int32 NumberB = B->Object.Get() ? B->Object.Get()->GetFName().GetNumber() : 0;
				return NumberA < NumberB;
			}
		};
	}
	ObjectListView->RequestListRefresh();
}


void SUBrowser::OnObjectListSelectionChanged(TSharedPtr<FBrowserObject> InItem, ESelectInfo::Type SelectInfo)
{
	if (!InItem.IsValid())
	{
		return;
	}
	TArray< TWeakObjectPtr<UObject> > Selection;
	Selection.Add(InItem->Object);
	AddObjectToHistory(InItem);
	PropertyView->SetObjects(Selection);
	FBrowserObject* SelectedObject(InItem.Get());
	OnNewObjectView.Execute(SelectedObject);
}


FText SUBrowser::GetFilterClassText() const
{
	if (FilterClass)
	{
		return FilterClass->GetDisplayNameText();
	}

	return LOCTEXT("ClassFilter", "Class Filter");
}

FReply SUBrowser::OnClassSelectionClicked()
{
	const FText TitleText = LOCTEXT("PickClass", "Pick Class");

	FClassViewerInitializationOptions Options;
	Options.Mode = EClassViewerMode::ClassPicker;
	Options.bEnableClassDynamicLoading = true;
	Options.bShowDisplayNames = false;
	Options.bShowNoneOption = true;
	Options.bShowObjectRootClass = true;
	Options.bShowUnloadedBlueprints = true;

	UClass* ChosenClass = FilterClass;
	const bool bPressedOk = SClassPickerDialog::PickClass(TitleText, Options, ChosenClass, UObject::StaticClass());
	if (bPressedOk)
	{
		FilterClass = ChosenClass;
		RefreshList();
	}

	return FReply::Handled();
}

void SUBrowser::OnNewHostTextCommited(const FText& InText, ETextCommit::Type InCommitType)
{
	FilterText = InText;
	FilterString = FilterText.ToString();

	RefreshList();
}

/** Called when a node is double clicked */
void SUBrowser::OnNodeDoubleClicked(class UEdGraphNode* Node)
{
	if (Node != nullptr) {
		TArray< TWeakObjectPtr<UObject> > Selection;
		const UObject* NodeObject = Cast<UBrowseNode>(Node)->GetUObject();
		Selection.Add(NodeObject);
		PropertyView->SetObjects(Selection);
		AddObjectToHistory(TSharedPtr<FBrowserObject>(new FBrowserObject(NodeObject)));
	}
}

FReply SUBrowser::OnCollectGarbage()
{
	::CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	return FReply::Handled();
}

EColumnSortMode::Type SUBrowser::GetSortMode() const
{
	return SortDirection;
}

void SUBrowser::OnSortByChanged(const EColumnSortPriority::Type SortPriority, const FName& ColumnName, const EColumnSortMode::Type NewSortMode)
{
	if (ColumnName == "Class")
	{
		SortBy = EQuerySortMode::ByType;
	}
	else if (ColumnName == "Name")
	{
		SortBy = EQuerySortMode::ByID;
	}
	else if (ColumnName == "Number")
	{
		SortBy = EQuerySortMode::ByNumber;
	}
	RefreshList();
}


void FBrowserObject::CustomizeDetails(IDetailLayoutBuilder& DetailLayout)
{
	struct UBrowseRowBuilder
	{
		IDetailCategoryBuilder& Category;
		IDetailGroup& Group;
		const IDetailsView& View;

		UBrowseRowBuilder(const IDetailsView&  RowView, IDetailCategoryBuilder& RowCategory, IDetailGroup& CategoryGroup) : View(RowView), Category(RowCategory), Group(CategoryGroup)
		{

		}


		void operator()(const FString& RowName, const FString& NameText, const FString& ValueText, const FString& TooltipText, UObject* Context)
		{
			auto OnClickedLambda = [Context]() -> FReply
			{
				FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
				TSharedPtr<IDetailsView> ParentView = EditModule.FindDetailView(FName(TEXT("UBrowse")));
				FWidgetPath WidgetPath;
				FSlateApplication::Get().FindWidgetWindow(ParentView.ToSharedRef(), WidgetPath);
				FArrangedChildren& ArrangedChildren = WidgetPath.Widgets;
				for (int iWidget = 0; iWidget < ArrangedChildren.Num(); iWidget++)
				{
					FArrangedWidget ArrangedWidget = ArrangedChildren[iWidget];
					if (ArrangedWidget.Widget->GetTag() == FName(TEXT("UBrowseTag"))) {
						TSharedPtr<SWidget> WidgetPtr(ArrangedWidget.Widget);
						TSharedPtr<SUBrowser> UBrowserWidget = StaticCastSharedPtr<SUBrowser>(WidgetPtr);
						TSharedPtr<FBrowserObject> SelectedObject(new FBrowserObject);
						SelectedObject->Object = Context;
						UBrowserWidget->OnObjectListSelectionChanged(SelectedObject, ESelectInfo::Direct);
					}
				}
				return FReply::Handled();
			};

			Group.AddWidgetRow()
				.NameContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.HAlign(HAlign_Left)
					[
						SNew(SButton)
						.OnClicked_Lambda(OnClickedLambda)
						.Content()
						[
							SNew(STextBlock)
							.Text(FText::FromString(NameText))
						.ToolTipText(FText::FromString(RowName))
						.Font(IDetailLayoutBuilder::GetDetailFont())

						]
					]
				
				]
				.ValueContent()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					[
						SNew(SEditableText)
						.IsReadOnly(true)
						.Text(FText::FromString(ValueText))
						.ToolTipText(FText::FromString(TooltipText))
						.Font(IDetailLayoutBuilder::GetDetailFont())
					]
				];
		}

		void operator()(const FString& RowName, const FString& NameText, const FString& ValueText, const FString& TooltipText)
		{
			Group.AddWidgetRow()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(NameText))
				.ToolTipText(FText::FromString(RowName))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				[
					SNew(SEditableText)
					.IsReadOnly(true)
					.Text(FText::FromString(ValueText))
					.ToolTipText(FText::FromString(TooltipText))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
			];
		}

	};

	struct BoolString
	{
		BoolString() {
		}

		const FString operator()(bool value, const FString& property)
		{
			return value ? "Is " + property : "Is Not " + property;
		}
		const FString& operator()(bool value)
		{
			return value ? TEXT("True") : TEXT("False");
		}
	};

	struct ObjectFlagBuilder
	{
		/**
		* Maps object flag to human-readable string.
		*/
		class FObjectFlag
		{
		public:
			EObjectFlags	ObjectFlag;
			const TCHAR*	FlagName;
			FObjectFlag(EObjectFlags InObjectFlag, const TCHAR* InFlagName)
				: ObjectFlag(InObjectFlag)
				, FlagName(InFlagName)
			{}
		};

		/**
		* Initializes the singleton list of object flags.
		*/
		static TArray<FObjectFlag> PrivateInitObjectFlagList()
		{
			TArray<FObjectFlag> ObjectFlagList;
#ifdef	DECLARE_OBJECT_FLAG
#error DECLARE_OBJECT_FLAG already defined
#else
#define DECLARE_OBJECT_FLAG( ObjectFlag ) ObjectFlagList.Add( FObjectFlag( RF_##ObjectFlag, TEXT(#ObjectFlag) ) );
			DECLARE_OBJECT_FLAG(ClassDefaultObject)
			DECLARE_OBJECT_FLAG(DefaultSubObject)
			DECLARE_OBJECT_FLAG(LoadCompleted)
			DECLARE_OBJECT_FLAG(ArchetypeObject)
			DECLARE_OBJECT_FLAG(Transactional)
			DECLARE_OBJECT_FLAG(Public)
			DECLARE_OBJECT_FLAG(TagGarbageTemp)
			DECLARE_OBJECT_FLAG(NeedLoad)
			DECLARE_OBJECT_FLAG(Transient)
			DECLARE_OBJECT_FLAG(Standalone)
			DECLARE_OBJECT_FLAG(BeginDestroyed)
			DECLARE_OBJECT_FLAG(FinishDestroyed)
			DECLARE_OBJECT_FLAG(NeedPostLoad)
#undef DECLARE_OBJECT_FLAG
#endif
			return ObjectFlagList;
		}

		/**
		* Dumps object flags from the selected objects to debugf.
		*/
		static FString PrintObjectFlags(UObject* Object)
		{
			static TArray<FObjectFlag> SObjectFlagList = PrivateInitObjectFlagList();

			if (Object)
			{
				FString Buf;
				for (int32 FlagIndex = 0; FlagIndex < SObjectFlagList.Num(); ++FlagIndex)
				{
					const FObjectFlag& CurFlag = SObjectFlagList[FlagIndex];
					if (Object->HasAnyFlags(CurFlag.ObjectFlag))
					{
						Buf += FString::Printf(TEXT("%s "), CurFlag.FlagName);
					}
				}
				return Buf;
			}
			return TEXT("None");
		}
	};

	const IDetailsView&  View = DetailLayout.GetDetailsView();
	const TArray<TWeakObjectPtr<UObject>> Objects = DetailLayout.GetDetailsView().GetSelectedObjects();
	IDetailCategoryBuilder& ObjectCategory = DetailLayout.EditCategory("UObject", FText::GetEmpty(), ECategoryPriority::Uncommon);
	check(Objects.Num() > 0);
	IDetailGroup& ObjectGroup = ObjectCategory.AddGroup("UObject", LOCTEXT("UObjectProperties", "Object  Properties"), true, true);
	ObjectGroup.HeaderRow()
		[
			SNew(STextBlock)
			.Text(FText::FromString("UObject"))
		.Font(IDetailLayoutBuilder::GetDetailFont())
		];
	for (auto iObject : Objects)
	{
		bool bIsClass = false;
		UObject* Obj = iObject.Get();
		UClass*  Class = Cast<UClass>(Obj);
		if (Class == nullptr) {
			Class = Obj->GetClass();
			bIsClass = false;
		}
		else {
			Obj = Class->GetDefaultObject();
			bIsClass = true;
		}
		BoolString BoolProp;
		UBrowseRowBuilder Builder(View, ObjectCategory, ObjectGroup);
		FString ObjName = GetNameSafe(Obj);
		FString FullName = GetFullNameSafe(Obj);
		FString PathName = GetPathNameSafe(Obj);
		Builder(TEXT("Name"), TEXT("Name"), ObjName, FullName);
		FString DetailedInfo = Obj->GetDetailedInfo();
		if (DetailedInfo.IsEmpty())
			DetailedInfo = PathName;
		Builder(TEXT("Path Name"), TEXT("Path Name"), PathName, DetailedInfo);
		Builder(TEXT("Flags"), TEXT("Flags"), ObjectFlagBuilder::PrintObjectFlags(Obj), TEXT("Object Flags"));
		FString IsNativeText = BoolProp(Obj->IsNative(), TEXT("Native"));
		Builder(TEXT("Native"), TEXT("Native"), IsNativeText, IsNativeText);
		if (Class) {
			auto ClassName = GetNameSafe(Class);
			Builder(TEXT("Class"), TEXT("Class"), ClassName, GetFullNameSafe(Class), Class);
			auto CDO = Class->GetDefaultObject();
			if (CDO) {
				auto CDOName = GetNameSafe(CDO);
				Builder(TEXT("CDO"), TEXT("CDO"), CDOName, GetFullNameSafe(CDO), CDO);
			}
			FString ClassHeaderPath;
			if (FSourceCodeNavigation::FindClassHeaderPath(Class, ClassHeaderPath) && IFileManager::Get().FileSize(*ClassHeaderPath) != INDEX_NONE)
			{
				ClassHeaderPath = FPaths::GetCleanFilename(ClassHeaderPath);
				Builder(TEXT("CPPHeader"), TEXT("CPPHeader"), ClassHeaderPath, TEXT("Class Header Path"));
			}
		}
		UObject* Outer = Obj->GetOuter();
		if (Outer)
		{
			auto OuterName = GetNameSafe(Outer);
			Builder(TEXT("Outer"), TEXT("Outer"), OuterName, GetFullNameSafe(Outer), Outer);
		}
		UObject* Archetype = Obj->GetArchetype();
		if (Archetype) {
			auto ArchetypeName = GetNameSafe(Archetype);
			Builder(TEXT("Archetype"), TEXT("Archetype"), ArchetypeName, GetFullNameSafe(Archetype), Archetype);
		}
		FString SubObjectText = BoolProp(Obj->IsDefaultSubobject(), TEXT("Default SubObject"));
		Builder(TEXT("Default SubObject"), TEXT("SubObject"), SubObjectText, SubObjectText);
		UStruct *ObjectStruct = Cast<UStruct, UObject>(Obj);
		// ok, it's not a class or anything - it's an instance
		IDetailGroup& FieldGroup = ObjectCategory.AddGroup("UFields", LOCTEXT("UObjectFields", "Object Fields"), true, false);
		UBrowseRowBuilder ClassBuilder(View, ObjectCategory, FieldGroup);
		for (TFieldIterator<UProperty> PropIt(Class); PropIt; ++PropIt)
		{
			UProperty* Property = *PropIt;
			auto CPPName = Property->GetNameCPP();
			auto CPPType = Property->GetCPPType();
			auto PropertyName = Property->GetName();
			UClass* Owner = PropIt->GetOwnerClass();
			uint8* SourceAddr = Property->ContainerPtrToValuePtr<uint8>(Obj);
			if (SourceAddr != nullptr)
			{
				FString SourceValue;
				Property->ExportText_Direct(SourceValue, SourceAddr, SourceAddr, nullptr, PPF_ExportCpp);
				ClassBuilder(PropertyName, CPPName, SourceValue, CPPType, Property);
			}
		}
		/*
				} else {
					IDetailGroup& StructGroup = ObjectCategory.AddGroup("UStruct", LOCTEXT("UStructProperties", "Class Properties"), true, false);
					UBrowseRowBuilder StructBuilder(View, ObjectCategory, StructGroup);
					for (TFieldIterator<UProperty> PropIt(ObjectStruct); PropIt; ++PropIt)
					{
						UProperty* Property = *PropIt;
						auto CPPName = Property->GetNameCPP();
						auto CPPType = Property->GetCPPType();
						auto PropertyName = GetNameSafe(Property);
						auto OwnerName = GetNameSafe(Owner);
						Property->ExportTextItem()
						//StructBuilder(Property);
						//FName PathName(*(Property->GetPathName()));
						//StructGroup.AddPropertyRow(Property->GetFName());
					}
					if (Class) {
						UClass *Within = ObjectClass->ClassWithin;
						StructBuilder(TEXT("Within"), TEXT("Within"), GetNameSafe(Within), GetFullNameSafe("Within"), Within);
						UObject *GeneratedBy = ObjectClass->ClassGeneratedBy;
						StructBuilder(TEXT("GeneratedBy"), TEXT("GeneratedBy"), GetNameSafe(Within), GetFullNameSafe(Within), GeneratedBy);
					}
				}
		*/
		/*
				UBlueprint *ObjectBP = Cast<UBlueprint, UObject>(iObject);
				if (ObjectBP) {
					IDetailGroup& BPGroup = ObjectCategory.AddGroup("UBlueprint", LOCTEXT("UBlueprint Properties", "UBlueprint Properties"), true, false);
					UBrowseRowBuilder BPBuilder(View, ObjectCategory, BPGroup);
					auto Category = ObjectBP->BlueprintCategory;
					auto Description = ObjectBP->BlueprintDescription;
					auto BPClass = ObjectBP->GetBlueprintClass()->GetName();
					if (!Category.IsEmpty())
						BPBuilder(TEXT("Category"), TEXT("Category"), Category);
					if (!Description.IsEmpty())
						BPBuilder(TEXT("Description"), TEXT("Description"), Description);
					BPBuilder(TEXT("BPClass"), TEXT("BPClass"), BPClass, ObjectBP->GetBlueprintClass());
				}
				*/
	}
	return;
}

void SUBrowser::AddObjectToHistory(TSharedPtr<FBrowserObject> Item)
{
	History.Add(TSharedPtr<FBrowserObject>(Item));
	ObjectHistoryView->RequestListRefresh();
}

TSharedRef<ITableRow> SUBrowser::OnGenerateObjectListRow(TSharedPtr<FBrowserObject> ObjectPtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SUBrowserTableRow, OwnerTable)
		.Object(ObjectPtr)
		.HighlightText(FilterText);
}

TSharedRef<ITableRow> SUBrowser::HandlePropertyGenerateRow(TSharedPtr<FBrowserObject> ObjectPtr, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SUBrowsePropertyTableRow, OwnerTable)
		.Object(ObjectPtr)
		.HighlightText(FilterText);
}

/*
void SUBrowser::OnGetChildrenForTree(TWeakObjectPtr<UObject> InClass, TArray< TWeakObjectPtr<UObject> >& OutChildren) {
	// what classes derive from this class
	TArray<UClass*> Results;
	GetDerivedClasses(Cast<UClass>(InClass.Get()), Results, false);
	for (auto IClass : Results) {
		check(IClass->IsValidLowLevel());
		auto child = TWeakObjectPtr<UObject>(Cast<UObject>(IClass));
		check(child->IsValidLowLevel());
		OutChildren.Add(child);
	}
}

/** @return A widget to represent a data item in the TreeView */
TSharedRef<ITableRow> SUBrowser::OnGenerateHistoryRow(TSharedPtr<FBrowserObject> InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	return SNew(SUBrowserTableRow, OwnerTable).Object(InItem);
}

void SUBrowser::OnHistorySelectionChanged(TSharedPtr<FBrowserObject> InItem, ESelectInfo::Type /*SelectInfo*/)
{
	// ...
	TArray< TWeakObjectPtr<UObject> > Selection;
	const UObject* HistoryObject = InItem->Object.Get();
	Selection.Add(HistoryObject);
	PropertyView->SetObjects(Selection);
	FBrowserObject* SelectedObject(InItem.Get());
	OnNewObjectView.Execute(SelectedObject);
	// TODO : Change view
	/*
	int32 Index = History.IndexOfByPredicate([&InItem](TSharedPtr< FBrowserObject > Item) {
		return InItem->Object == Item->Object;
	});
	*/
	//ObjectHistoryView->SetItem Expansion(InItem, !ObjectHistoryView->IsItemExpanded(InItem));
}

void SUBrowser::PopulateHistoryList()
{
	History.Empty();
	TSharedPtr< FBrowserObject >  InitialObject(new FBrowserObject(UObject::StaticClass()));
	History.Add(InitialObject);
	OnNewObjectView.Execute(InitialObject.Get());
}

/*
TSharedPtr<SWidget> SUBrowser::GetTreeContextMenu()
{
	return
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FCoreStyle::Get().GetBrush("Menu.Background"))
			.Padding(FMargin(5))
			[
				SNew(STextBlock).Text(LOCTEXT("TreeContextMenuLabel", "Tree Context Menu"))
			]
		]
	+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STestMenu)
		];
}
*/

#undef LOCTEXT_NAMESPACE
