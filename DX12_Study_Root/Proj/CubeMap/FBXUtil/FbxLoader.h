//--------------------------------------------------------------------------
// AutoDesk FBX SDK에서 제공하는 API를 통해서 FBX파일에서 스키닝 메시에 필요한
// 자료들만 추출하여 dx12에서 사용할 수 있도록 변환해주는 클래스
// 아직 fbxsdk에서 제공하는 api들을 모두 학습하기엔 어려워서 일단 fbx파일 로드하는 부분만을 구현하였음.
// 로드를 끝내면 fbx 관련 객체들은 그냥 해제하도록 하고
// 그 데이터를 기반으로 렌더링하는 부분은 기존 dx12을 기반으로 구현하였음.
//--------------------------------------------------------------------------
#pragma once

#include <vector>
#include <string>
#include <unordered_map>

#include "SkinnedData.h"

// SDK의 모든 타입은 fbxsdk 네임스페이스 안에 있으므로 반드시 그 안에 선언해야 함
// 처음엔 네임스페이스 없이 전방 선언만 했더니 컴파일러가 이름만 같고 아예 다른 타입으로 인식해서 fbxsdk데이터와 모호하다고 에러가 나서
// fbxsdk와 같은 네임스페이스 안에 전방 선언을 해주고 using으로 전역에 올려서 사용하도록 함
// fbxsdk.h가 상당히 무거운데 이 헤더에서 include하면 최적화 측면에서 좋지 않기에 전방 선언만 해놓고 cpp에서만 include하도록 함
namespace fbxsdk
{
    class FbxManager;
    class FbxScene;
    class FbxNode;
    class FbxMesh;
    class FbxAnimStack;
}

using fbxsdk::FbxManager;
using fbxsdk::FbxScene;
using fbxsdk::FbxNode;
using fbxsdk::FbxMesh;
using fbxsdk::FbxAnimStack;

// 스키닝 정점
// 기존에 내가 작업하던 프로젝트엔 아직 노멀맵을 추가하진 않은 상태라서 텍스처 좌표만
// SkinnedVertex의 레이아웃 오프셋은 DX12에서 정의한 mSkinnedInputLayout과 정확히 일치해야 에러가 없음
// 네트워크에서 패킷 크기를 패딩을 통해 맞춰주는 것과 비슷함
struct SkinnedVertex
{
    DirectX::XMFLOAT3 Pos;            // offset  0  POSITION
    DirectX::XMFLOAT3 Normal;         // offset 12  NORMAL
    DirectX::XMFLOAT2 TexC;           // offset 24  TEXCOORD
    DirectX::XMFLOAT3 BoneWeights;    // offset 32  WEIGHTS     
    std::uint8_t      BoneIndices[4]; // offset 44  BONEINDICES
};

//사전에 레이아웃 크기 검사
static_assert(sizeof(SkinnedVertex) == 48,
    "SkinnedVertex 크기가 레이아웃 오프셋(0/12/24/32/44)과 일치하지 않습니다.");


// FBX파일에서 스키닝 메시에 필요한자료들만 추출하여 dx12에서 사용할 수 있도록 변환해주는 클래스
class FbxLoader
{
public:

    //복사를 허용하면 FbxManager와 Fbx객체 타입들이 얕은 복사가 일어나서 double free 및 댕글링포인터가 되기에 delete
    FbxLoader() = default;
    ~FbxLoader();
    FbxLoader(const FbxLoader& ref) = delete;
    FbxLoader& operator=(const FbxLoader& ref) = delete;

    // 메인 로드 함수 
    bool LoadFBX(const std::string& filename,
        std::vector<SkinnedVertex>& vertices,
        std::vector<std::uint32_t>& indices,
        SkinnedData& skinnedInfo);

    // LoadFBX 실패 시 마지막 에러 메시지 반환
    const std::string& GetErrorMsg() const { return mLastError; }

    // 애니메이션 샘플링 간격 기본 30fps
    void SetSampleRate(float sec) { mSampleRate = sec; }

    // 단계마다 데이터 검증용 디버깅 로그를 통해 출력할지 설정하는 함수
    // true - 출력o false - 출력x
    void SetVerbose(bool verbose) { mVerbose = verbose; }

    // 로드 후에도 유효한 본 이름 목록 ImGui 표시에 사용할 예정
    const std::vector<std::string>& GetBoneNames() const { return mBoneNames; }
    const std::vector<std::string>& GetClipNames() const { return mClipNames; }

private:
    // 컨트롤 포인트(=fbx에서는 위치에 대한 정보만 있는 정점) 하나에 영향을 본 인덱스와 그 가중치
    // pair<int, float>로 하려다가 .first, .second가 가독성이 안 좋을 것 같고
    // 무엇보다 FBXLoader 내부에서만 쓰고 그 결과를 외부에 반환하지 않아서
    // private 구조체로 선언
    struct BoneWeight
    {
        int   BoneIndex = 0;
        float Weight = 0.0f;
    };

    // SDK 관련 데이터들 초기화 및 파일 임포트
    bool InitializeSDK(const std::string& filename);

    // 좌표계 변환, 다각형 폴리곤 삼각형화
    void PreprocessScene();

    // 본 계층 구축
    void BuildSkeleton();
    void TraverseSkeleton(FbxNode* node, int parentIndex);

    // 스킨 가중치 + 본 오프셋 행렬 세팅
    bool ExtractSkinWeights(FbxMesh* mesh);
    // 가중치 정규화
    void NormalizeWeights();

    // 메시의 정점, 인덱스 추출
    bool ExtractMesh(FbxMesh* mesh,
        std::vector<SkinnedVertex>& vertices,
        std::vector<std::uint32_t>& indices);
    //SkinnedVertex의 본 인덱스,가중치 세팅
    void FillSkinData(int cpIndex, SkinnedVertex& vertex) const;

    //  애니메이션 데이터 추출
    bool ExtractAnimations(std::unordered_map<std::string, AnimationClip>& animations);
    void SampleClip(FbxAnimStack* animStack, AnimationClip& clip);

    // 데이터 검증용 디버깅 로그함수 mVerbose가 true일 때만 동작
    void CheckSkeletonInfo() const;
    void CheckSkinInfo() const;
    void CheckMeshInfo(FbxMesh* mesh,
        const std::vector<SkinnedVertex>& vertices,
        const std::vector<std::uint32_t>& indices) const;
    void CheckAnimationInfo(
        const std::unordered_map<std::string, AnimationClip>& animations) const;

    //애니메이션 커브 존재 여부 확인
    bool HasAnimationCurves(FbxAnimStack* animStack) const;

    // fbxsdk관련 자원 해제
    void Cleanup();

private:
    // fbxsdk에서 제공하는 Create() 써서 메모리를 할당해야 하므로 포인터로 선언 해제 또한 delete가 아닌 별도의 Destroy()
    FbxManager* mSdkManager = nullptr;
    FbxScene* mScene = nullptr;

    // BuildSkeleton()에서 채움
    std::vector<int>                  mBoneHierarchy;  // 본의 부모 인덱스 (루트는 -1)
    std::vector<FbxNode*>             mBoneNodes;   
    std::unordered_map<FbxNode*, int> mBoneIndexMap;   // 본 노드 포인터에 대한 인덱싱을 저장하는 데이터
    std::vector<std::string>          mBoneNames;      

    // ExtractSkinWeights()에서 채움
    std::vector<DirectX::XMFLOAT4X4>     mBoneOffsets;
    std::vector<std::vector<BoneWeight>> mControlPointWeights;
    std::vector<std::string>             mClipNames;              

    //Util관련 세팅 변수들
    float mSampleRate = 1.0f / 30.0f; //30fps
    bool  mVerbose = false;
    std::string mLastError;
};