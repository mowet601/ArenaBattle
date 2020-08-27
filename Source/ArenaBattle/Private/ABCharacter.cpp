// Fill out your copyright notice in the Description page of Project Settings.

#include "ABCharacter.h"
#include "ABAnimInstance.h"
#include "ABWeapon.h"
#include "ABCharacterStatComponent.h"
#include "DrawDebugHelpers.h"
#include "Components/WidgetComponent.h"
#include "ABCharacterWidget.h"
#include "ABAIController.h"
#include "ABCharacterSetting.h" // 데이터
#include "ABGameInstance.h" // 명령어
#include "ABPlayerController.h" 

// 초기화 및 프레임별 설정
AABCharacter::AABCharacter()
{
 	// Set this character to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SPRINGARM"));
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("CAMERA"));
	CharacterStat = CreateDefaultSubobject<UABCharacterStatComponent>(TEXT("CHARACTERSTAT"));
	HPBarWidget = CreateDefaultSubobject<UWidgetComponent>(TEXT("HPBARWIDGET"));

	SpringArm->SetupAttachment(GetCapsuleComponent());
	Camera->SetupAttachment(SpringArm);
	HPBarWidget->SetupAttachment(GetMesh());

	GetMesh()->SetRelativeLocationAndRotation(FVector(0.0f, 0.0f, -88.0f), FRotator(0.0f, -90.0f, 0.0f)); // 캐릭터 위치 설정
	SpringArm->TargetArmLength = 400.0f;
	SpringArm->SetRelativeRotation(FRotator(-15.0f, 0.0f, 0.0f));

	static ConstructorHelpers::FObjectFinder<USkeletalMesh> SK_CARDBOARD(TEXT("/Game/InfinityBladeWarriors/Character/CompleteCharacters/SK_CharM_Cardboard.SK_CharM_Cardboard"));
	if (SK_CARDBOARD.Succeeded()) // 캐릭터 스킨
	{
		GetMesh()->SetSkeletalMesh(SK_CARDBOARD.Object);
	}
	GetMesh()->SetAnimationMode(EAnimationMode::AnimationBlueprint);

	static ConstructorHelpers::FClassFinder<UAnimInstance> WARRIOR_ANIM(TEXT("/Game/Animations/WarriorAnimBlueprint.WarriorAnimBlueprint_C"));
	if (WARRIOR_ANIM.Succeeded()) // 캐릭터 애니메이션
	{
		GetMesh()->SetAnimInstanceClass(WARRIOR_ANIM.Class);
	}
	SetControlMode(EControlMode::DIABLO); // 카메라(조작)
	ArmLengthSpeed = 3.0f;
	ArmRotationSpeed = 10.0f;
	GetCharacterMovement()->JumpZVelocity = 800.0f;
	IsAttacking = false;
	MaxCombo = 4;
	AttackEndComboState();
	GetCapsuleComponent()->SetCollisionProfileName(TEXT("ABCharacter"));
	AttackRange = 200.0f;
	AttackRadius = 50.0f;

	HPBarWidget->SetRelativeLocation(FVector(0.0f, 0.0f, 180.0f));
	HPBarWidget->SetWidgetSpace(EWidgetSpace::Screen);
	static ConstructorHelpers::FClassFinder<UUserWidget> UI_HUD(TEXT("WidgetBlueprint'/Game/Book/UI/UI_HPBar.UI_HPBar_C'"));

	if (UI_HUD.Succeeded()) // HUD 설정, ABCharacter에서는 HP바의 기능을 구현하기보다, 출력할 위치 설정과 드로우를 담당한다.
	{
		HPBarWidget->SetWidgetClass(UI_HUD.Class);
		HPBarWidget->SetDrawSize(FVector2D(150.0f, 50.0f));
	}

	DeadTimer = 5.0f;
	AIControllerClass = AABAIController::StaticClass();
	AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
	// PREINIT 스테이트
	// 캐릭터는 최초로 PREINIT(생성자)에서 시작한다. 그러다가 게임이 시작돼 BeginPlay()가 호출되면, LOADING 스테이트로 넘어간다.
	AssetIndex = 4;
	SetActorHiddenInGame(true); // 액터
	HPBarWidget->SetHiddenInGame(true); // UI
	SetCanBeDamaged(false); // 데미지 판정
	// 캐릭터 생성 전의 스테이트 : 캐릭터와 UI를 숨겨두고, 데미지를 입지 않게 한다.
}

void AABCharacter::PostInitializeComponents() // 델리게이트 초기화 설정
{
	Super::PostInitializeComponents();
	ABAnim = Cast<UABAnimInstance>(GetMesh()->GetAnimInstance());
	ABCHECK(nullptr != ABAnim);
	ABAnim->OnMontageEnded.AddDynamic(this, &AABCharacter::OnAttackMontageEnded); // OnMontageEnded 델리게이트는 애니메이션 몽타주에서 생성한 델리게이트

	ABAnim->OnNextAttackCheck.AddLambda([this]() -> void { // OnNextAttck 델리게이트에 등록한 함수, 연속 공격시 델리게이트가 호출함

		ABLOG_Long(Warning, TEXT("OnNextAttackCheck"));
		CanNextCombo = false;

		if (IsComboInputOn)
		{
			AttackStartComboState();
			ABAnim->JumpToAttackMontageSection(CurrentCombo);
		}
	});
	ABAnim->OnAttackHitCheck.AddUObject(this, &AABCharacter::AttackCheck); // 히트 판정 시 델리게이트가 호출할 함수

	CharacterStat->OnHPIsZero.AddLambda([this]()-> void { // hp가 0이 될 시 
		ABAnim->SetDeadAnim(); // isDead = true 죽는 모션
		SetActorEnableCollision(false); // 콜리전(물리) 비활성화
	}); // 델리게이트가 호출할 람다식

	HPBarWidget->InitWidget(); // ※ 4.25 버전 추가사항 : 사용자 위젯이 초기화되었는지 확인 필요!! UI 초기화는 BeginPlay()에서 호출되므로 미리 확인 필요
	auto CharacterWidget = Cast<UABCharacterWidget>(HPBarWidget->GetUserWidgetObject());

	if (nullptr != CharacterWidget)
	{
		CharacterWidget->BindCharacterStat(CharacterStat);
	}
	else
	{
		ABLOG_Short(Error);
	}
}

void AABCharacter::BeginPlay()
{
	Super::BeginPlay();
	// PREINIT 스테이트
	bIsPlayer = IsPlayerControlled(); // 캐릭터를 플레이어가 컨트롤 할 경우
	if (bIsPlayer)
	{
		ABPlayerController = Cast<AABPlayerController>(GetController()); // 플레이어 컨트롤러는 해당 캐릭터를 제어함
		ABCHECK(nullptr != ABPlayerController);
	}
	else
	{ // 아닐 경우
		ABAIController = Cast<AABAIController>(GetController()); // AI컨트롤러가 해당 캐릭터를 제어함
		ABCHECK(nullptr != ABAIController);
	}
	
	auto DefaultSetting = GetDefault<UABCharacterSetting>(); // 캐릭터 세팅(로드 부분)  ini파일을 불러옴
	if (bIsPlayer)
	{
		AssetIndex = 4; // 4번 인덱스의 캐릭터 에셋(플레이어용 박스 워리어)
	}
	else
	{
		AssetIndex = FMath::RandRange(0, DefaultSetting->CharacterAssets.Num() - 1); // 랜덤으로 하나의 에셋을 골라 적용
	} // 에셋 로딩
	CharacterAssetToLoad = DefaultSetting->CharacterAssets[AssetIndex]; // 캐릭터 에셋 로드(랜덤 또는 4번 에셋)
	auto ABGameInstance = Cast<UABGameInstance>(GetGameInstance()); // StreamableManager를 사용하기 위해 GameInstance 클래스 파일을 객체화(캐스팅)
	ABCHECK(nullptr != ABGameInstance);  // 비동기 방식으로 애셋을 로딩할 때 델리게이트(OnAssetLoadCompleted)를 호출하도록 등록, 캐스팅된 클래스에서 멤버함수를 호출하여 비동기 방식으로(ReqAsycLoad) 애셋을 로딩하도록 한다.
	AssetStreamingHandle = ABGameInstance->StreamableManager.RequestAsyncLoad(CharacterAssetToLoad, FStreamableDelegate::CreateUObject(this, &AABCharacter::OnAssetLoadCompleted));  // 데이터(에셋 경로)는 ABCharacterSetting에서, 명령어(비동기 애셋 로딩 로직)은 GameInstance에서 사용한다! 데이터와 명령어 구분함
	SetCharacterState(ECharacterState::LOADING); // LOADING 스테이트로 전이

/*	if (!IsPlayerControlled()) // 플레이어가 아닐 경우 (구 모듈)
	{
		auto DefaultSetting = GetDefault<UABCharacterSetting>(); // ini파일을 불러옴
		int32 RandIndex = FMath::RandRange(0, DefaultSetting->CharacterAssets.Num() - 1); // 파일 길이 만큼의 범위로 랜덤 난수 지정
		CharacterAssetToLoad = DefaultSetting->CharacterAssets[RandIndex]; // 랜덤 인덱스를 사용하여 무작위 캐릭터 애셋 로드

		auto ABGameInstance = Cast<UABGameInstance>(GetGameInstance()); // StreamableManager를 사용하기 위해 GameInstance 클래스 파일을 객체화(캐스팅)
		if (nullptr != ABGameInstance)
		{ // 비동기 방식으로 애셋을 로딩할 때 델리게이트(OnAssetLoadCompleted)를 호출하도록 등록
			AssetStreamingHandle = ABGameInstance->StreamableManager.RequestAsyncLoad(CharacterAssetToLoad, FStreamableDelegate::CreateUObject(this, &AABCharacter::OnAssetLoadCompleted)); // 캐스팅된 클래스에서 멤버함수를 호출하여 비동기 방식으로(ReqAsycLoad) 애셋을 로딩하도록 한다.
		}																									// CreateUObject()를 사용해 즉석에서 델리게이트를 생성하여 넘겨준다.
	} // 데이터는 ABCharacterSetting에서, 명령어(비동기 애셋 로딩)은 GameInstance에서 사용한다! */
}

void AABCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	SpringArm->TargetArmLength = FMath::FInterpTo(SpringArm->TargetArmLength, ArmLengthTo, DeltaTime, ArmLengthSpeed);

	switch (CurrentControlMode) // 카메라가 벽에 부딪힐 시 시야처리
	{
	case AABCharacter::EControlMode::GTA:
		break;
	case AABCharacter::EControlMode::DIABLO:
		SpringArm->SetRelativeRotation(FMath::RInterpTo(SpringArm->GetRelativeRotation(), ArmRotationTo, DeltaTime, ArmRotationSpeed));
		break;
	default:
		break;
	}

	switch (CurrentControlMode) // 게임모드별 카메라 시야처리
	{
	case AABCharacter::EControlMode::GTA:
		break;
	case AABCharacter::EControlMode::DIABLO:
		if (DirectionToMove.SizeSquared() > 0.0f)
		{
			GetController()->SetControlRotation(FRotationMatrix::MakeFromX(DirectionToMove).Rotator());
			AddMovementInput(DirectionToMove);
		}
		break;
	default:
		break;
	}


}


// 이동 및 카메라 시스템
void AABCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent) // 입력장치 설정
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	PlayerInputComponent->BindAction(TEXT("ViewChange"), EInputEvent::IE_Pressed, this, &AABCharacter::ViewChange);
	PlayerInputComponent->BindAxis(TEXT("UpDown"), this, &AABCharacter::UpDown);
	PlayerInputComponent->BindAxis(TEXT("LeftRight"), this, &AABCharacter::LeftRight);
	PlayerInputComponent->BindAxis(TEXT("LookUp"), this, &AABCharacter::LookUp);
	PlayerInputComponent->BindAxis(TEXT("Turn"), this, &AABCharacter::Turn);
	PlayerInputComponent->BindAction(TEXT("Jump"), EInputEvent::IE_Pressed, this, &ACharacter::Jump);
	PlayerInputComponent->BindAction(TEXT("Attack"), EInputEvent::IE_Pressed, this, &AABCharacter::Attack);
}

void AABCharacter::SetControlMode(EControlMode NewControlMode) // 모드별 설정
{
	CurrentControlMode = NewControlMode;
	switch (CurrentControlMode)
	{
	case EControlMode::GTA:
		ArmLengthTo = 450.0f;
		SpringArm->bUsePawnControlRotation = true;
		SpringArm->bInheritPitch = true;
		SpringArm->bInheritRoll = true;
		SpringArm->bInheritYaw = true;
		SpringArm->bDoCollisionTest = true;
		bUseControllerRotationYaw = false;
		GetCharacterMovement()->bOrientRotationToMovement = true;
		GetCharacterMovement()->bUseControllerDesiredRotation = false;
		GetCharacterMovement()->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
		break;
	case EControlMode::DIABLO:
		ArmLengthTo = 800.0f;
		ArmRotationTo = FRotator(-45.0f, 0.0f, 0.0f);
		SpringArm->bUsePawnControlRotation = false;
		SpringArm->bInheritPitch = false;
		SpringArm->bInheritRoll = false;
		SpringArm->bInheritYaw = false;
		SpringArm->bDoCollisionTest = false;
		bUseControllerRotationYaw = false;
		GetCharacterMovement()->bOrientRotationToMovement = false;
		GetCharacterMovement()->bUseControllerDesiredRotation = true;
		GetCharacterMovement()->RotationRate = FRotator(0.0f, 720.0f, 0.0f);
		break;
	case EControlMode::NPC:
		bUseControllerRotationYaw = false;
		GetCharacterMovement()->bUseControllerDesiredRotation = false;
		GetCharacterMovement()->bOrientRotationToMovement = true;
		GetCharacterMovement()->RotationRate = FRotator(0.0f, 480.0f, 0.0f);
		break;
	}
}

void AABCharacter::UpDown(float NewAxisValue) // 캐릭터 상하
{
	switch (CurrentControlMode)
	{
	case AABCharacter::EControlMode::GTA:
		AddMovementInput(FRotationMatrix(FRotator(0.0f, GetControlRotation().Yaw, 0.0f)).GetUnitAxis(EAxis::X), NewAxisValue);
		break;
	case AABCharacter::EControlMode::DIABLO:
		DirectionToMove.X = NewAxisValue;
		break;
	default:
		break;
	}
}

void AABCharacter::LeftRight(float NewAxisValue) // 캐릭터 좌우
{
	switch (CurrentControlMode)
	{
	case AABCharacter::EControlMode::GTA:
		AddMovementInput(FRotationMatrix(FRotator(0.0f, GetControlRotation().Yaw, 0.0f)).GetUnitAxis(EAxis::Y), NewAxisValue);
		break;
	case AABCharacter::EControlMode::DIABLO:
		DirectionToMove.Y = NewAxisValue;
		break;
	default:
		break;
	}
}

void AABCharacter::LookUp(float NewAxisValue) // 카메라 상하 회전
{
	switch (CurrentControlMode)
	{
	case AABCharacter::EControlMode::GTA:
		AddControllerPitchInput(NewAxisValue);
		break;
	case AABCharacter::EControlMode::DIABLO: // 쿼터뷰는 위 아래 회전이 필요없다.
		break;
	default:
		break;
	}
}

void AABCharacter::Turn(float NewAxisValue) // 카메라 좌우 회전
{
	switch (CurrentControlMode)
	{
	case AABCharacter::EControlMode::GTA:
		AddControllerYawInput(NewAxisValue);
		break;
	case AABCharacter::EControlMode::DIABLO:
		break;
	default:
		break;
	}
}

void AABCharacter::ViewChange() // 카메라 모드 토글
{
		switch (CurrentControlMode)
		{
		case AABCharacter::EControlMode::GTA:
			GetController()->SetControlRotation(GetActorRotation());
			SetControlMode(EControlMode::DIABLO);
			break;
		case AABCharacter::EControlMode::DIABLO:
			GetController()->SetControlRotation(SpringArm->GetRelativeRotation());
			SetControlMode(EControlMode::GTA);
			break;
		default:
			break;
		}
}


// 전투 시스템 
bool AABCharacter::CanSetWeapon()
{
	return (nullptr == CurrentWeapon);
}

void AABCharacter::PossessedBy(AController* NewController) // 캐릭터의 빙의자가 누구인가(플레이어,npc), 기본 카메라 타입과 이동속도를 설정 
{
	Super::PossessedBy(NewController);
	if (IsPlayerControlled()) // 플레이어가 컨트롤할 경우
	{
		SetControlMode(EControlMode::DIABLO);
		GetCharacterMovement()->MaxWalkSpeed = 600.0f;
	}
	else // AI가 컨트롤 할 경우
	{
		SetControlMode(EControlMode::NPC);
		GetCharacterMovement()->MaxWalkSpeed = 300.0f; // 플레이어보다 좀 느리게
	}
}

void AABCharacter::SetWeapon(AABWeapon* NewWeapon) // 무기 장착
{
	ABCHECK(nullptr != NewWeapon && nullptr == CurrentWeapon);
	FName WeaponSocket(TEXT("hand_rSocket"));
	if (nullptr != NewWeapon)
	{
		NewWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, WeaponSocket);
		NewWeapon->SetOwner(this);
		CurrentWeapon = NewWeapon;
	}
}

void AABCharacter::Attack() // 공격
{
	ABLOG_Short(Warning);
	if (IsAttacking) // 이미 공격 중일시 콤보 지속
	{
		ABCHECK(FMath::IsWithinInclusive<int32>(CurrentCombo, 1, MaxCombo));
		if (CanNextCombo)
		{
			IsComboInputOn = true;
		}
	}
	else // 첫 공격 시 콤보 시작
	{
			ABCHECK(CurrentCombo == 0);
			AttackStartComboState();
			ABAnim->PlayAttackMontage();
			ABAnim->JumpToAttackMontageSection(CurrentCombo);
			IsAttacking = true;
	}
}

void AABCharacter::SetCharacterState(ECharacterState NewState)
{
	ABCHECK(CurrentState != NewState);
	CurrentState = NewState;

	switch (CurrentState)
	{
	case ECharacterState::LOADING: // LOADING 스테이트 설정 
	{
		if (bIsPlayer)
		{
			DisableInput(ABPlayerController); // 로딩 중 입력 비활성화
		}
		SetActorHiddenInGame(true);
		HPBarWidget->SetHiddenInGame(true);
		SetCanBeDamaged(false);
	}
	break;
	case ECharacterState::READY: // READY 스테이트 = 액터표시, UI, 데미지처리 표시
	{
		SetActorHiddenInGame(false);
		HPBarWidget->SetHiddenInGame(false);
		SetCanBeDamaged(true);
		CharacterStat->OnHPIsZero.AddLambda([this]()->void {
			SetCharacterState(ECharacterState::DEAD); // HP가 Zero면 DEAD 스테이트로 상태 전이되게 델리게이트에 등록
		});
		HPBarWidget->InitWidget();
		auto CharacterWidget = Cast<UABCharacterWidget>(HPBarWidget->GetUserWidgetObject());
		ABCHECK(nullptr != CharacterWidget);
		CharacterWidget->BindCharacterStat(CharacterStat);

		if (bIsPlayer) // 플레이어일 경우 카메라, 이동속도 할당
		{
			SetControlMode(EControlMode::DIABLO);
			GetCharacterMovement()->MaxWalkSpeed = 600.0f;
			EnableInput(ABPlayerController);
		}
		else
		{
			SetControlMode(EControlMode::NPC);
			GetCharacterMovement()->MaxWalkSpeed = 400.0f;
			ABAIController->RunAI(); // NPC일 경우 AI 트리를 실행시킨다.
		}
	}
	break;
	case ECharacterState::DEAD:
	{
		SetActorEnableCollision(false); // 물리 비중 제거
		GetMesh()->SetHiddenInGame(false);
		HPBarWidget->SetHiddenInGame(true);
		ABAnim->SetDeadAnim(); // 죽음 표시
		SetCanBeDamaged(false);

		if (bIsPlayer)
		{
			DisableInput(ABPlayerController);
		}
		else
		{
			ABAIController->StopAI(); // NPC일 경우 해동 트리를 중단시킨다.
		}
		GetWorld()->GetTimerManager().SetTimer(DeadTimerHandle, FTimerDelegate::CreateLambda([this]()->void { // 캐릭터가 죽을 경우 일정 타이머(5.0f) 후에 람다로 등록된 델리게이트 호출 SetTimer()
			if (bIsPlayer)
			{
				ABPlayerController->RestartLevel(); // 플레이어는 리스폰
			}
			else
			{
				Destroy(); // NPC는 삭제
			}
		}), DeadTimer, false); // DeadTimer가 일정시간이며, 5.0f로 설정되었다.
	}
		break;
	default:
		break;
	}
}

ECharacterState AABCharacter::GetCharacterState() const
{
	return ECharacterState();
}

void AABCharacter::AttackCheck() // 데미지 체크
{
	FHitResult HitResult;
	FCollisionQueryParams Params(NAME_None, false, this); // Parm 설정 
	bool bResult = GetWorld()->SweepSingleByChannel(
		HitResult,
		GetActorLocation(),
		GetActorLocation() + GetActorForwardVector() * 200.0f,
		FQuat::Identity,
		ECollisionChannel::ECC_GameTraceChannel2,
		FCollisionShape::MakeSphere(50.0f),
		Params);

#if ENABLE_DRAW_DEBUG
	FVector TraceVec = GetActorForwardVector() * AttackRange;
	FVector Center = GetActorLocation() + TraceVec * 0.5f;
	float HalfHeight = AttackRange * 0.5f + AttackRadius;
	FQuat CapsuleRot = FRotationMatrix::MakeFromZ(TraceVec).ToQuat();
	FColor DrawColor = bResult ? FColor::Green : FColor::Red;
	float DebugLifeTime = 5.0f;

	DrawDebugCapsule(GetWorld(),
		Center,
		HalfHeight,
		AttackRadius,
		CapsuleRot,
		DrawColor,
		false,
		DebugLifeTime);
#endif

	if (bResult)
	{
		if (HitResult.Actor.IsValid())
		{
			ABLOG_Long(Warning, TEXT("Hit Actor Name %s"), *HitResult.Actor->GetName());
			FDamageEvent DamageEvent;
			HitResult.Actor->TakeDamage(CharacterStat->GetAttack(), DamageEvent, GetController(), this);
		}
	}
}

void AABCharacter::OnAssetLoadCompleted()
{
	AssetStreamingHandle->ReleaseHandle();
	TSoftObjectPtr<USkeletalMesh> LoadAssetPath(CharacterAssetToLoad); // 애셋의 경로 정보에 해당되는 스태틱 메시 입히기
	ABCHECK(LoadAssetPath.IsValid());
	GetMesh()->SetSkeletalMesh(LoadAssetPath.Get());
	SetCharacterState(ECharacterState::READY); // 로드가 끝나면 READY로 전이
	
}

void AABCharacter::AttackStartComboState() // 콤보
{
	CanNextCombo = true;
	IsComboInputOn = false;
	ABCHECK(FMath::IsWithinInclusive<int32>(CurrentCombo, 0, MaxCombo - 1));
	CurrentCombo = FMath::Clamp<int32>(CurrentCombo + 1, 1, MaxCombo);
}

void AABCharacter::AttackEndComboState() // 콤보 마지막
{
	IsComboInputOn = false;
	CanNextCombo = false;
	CurrentCombo = 0;
}

void AABCharacter::OnAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted) // 공격 애니메이션이 끝났으니 IsAttacking 변수를 false로 변경
{
	ABCHECK(IsAttacking);
	ABCHECK(CurrentCombo > 0);
	IsAttacking = false;
	AttackEndComboState();
	OnAttackEnd.Broadcast();
}

float AABCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser) // 데미지 계산
{
	float FinalDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	ABLOG_Long(Warning, TEXT("Actor : %s took Damage : %f"), *GetName(), FinalDamage);

	CharacterStat->SetDamage(FinalDamage); // 최종 데미지 전달
	return FinalDamage;
}