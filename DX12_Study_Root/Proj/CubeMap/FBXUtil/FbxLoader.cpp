#include "FbxLoader.h"
#include "FbxUtil.h"

#include <fbxsdk.h>

using namespace DirectX;
using namespace FbxUtil;

FbxLoader::~FbxLoader()
{
    Cleanup();
}

void FbxLoader::Cleanup()
{
    // FbxManager::Destroy()가 노드를 전부 해제하므로
    // 노드 포인터를 들고 있는 컨테이너를 먼저 비움 -> 댕글링 포인터 방지
    mBoneNodes.clear();
    mBoneIndexMap.clear();
    mControlPointWeights.clear();

    if (mSdkManager != nullptr)
    {
        // 매니저가 자신이 만든 모든 객체(Scene 포함)를 함께 해제함
        mSdkManager->Destroy();
        mSdkManager = nullptr;
        mScene = nullptr;
    }
}


//메인 함수
bool FbxLoader::LoadFBX(const std::string& filename,
                     std::vector<SkinnedVertex>& vertices,
                     std::vector<std::uint32_t>& indices,
                     SkinnedData& skinnedInfo)
{
    //에러 로그 비워줌
    mLastError.clear();

    // SDK 초기화 및 파일 임포트
    if (InitializeSDK(filename) == false)
    {
        Cleanup();
        return false;
    }

    // 좌표계, 폴리곤 동기화를 위한 씬 전처리함수
    // Triangulate가 메시 객체를 변경하므로
    // 반드시 메시를 찾기 전에 호출해야 함
    PreprocessScene();


    FbxMesh* mesh = FindFirstMesh(mScene->GetRootNode());
    if (mesh == nullptr)
    {
        mLastError = "메시를 찾을 수 없습니다.";
        Cleanup();
        return false;
    }

    // 스켈레톤
    // 스킨 가중치와 애니메이션이 본 인덱스를 참조하므로 가장 먼저 확정한다.
    BuildSkeleton();
    if (mBoneHierarchy.empty())
    {
        mLastError = "스켈레톤(본)을 찾을 수 없습니다.";
        Cleanup();
        return false;
    }
    CheckSkeletonInfo();

    // 스킨 가중치 추출
    // 반드시 BuildSkeleton을 통해 본 상속 계층을 확정한 뒤에 호출해야 함.
    if (ExtractSkinWeights(mesh) == false)
    {
        Cleanup();
        return false;
    }
    CheckSkinInfo();

    // 메시 추출( mControlPointWeights)
    // ExtractSkinWeights을 통해 추출한 mControlPointWeights를 사용하므로 반드시 그 뒤에 호출해야 함.
    if (ExtractMesh(mesh, vertices, indices) == false)
    {
        Cleanup();
        return false;
    }
    CheckMeshInfo(mesh, vertices, indices);

    // 애니메이션 추출
    // 해시맵을 통해 이름을 애니메이션을 관리함.
    std::unordered_map<std::string, AnimationClip> animations;
    if (ExtractAnimations(animations) == false)
    {
        Cleanup();
        return false;
    }
    CheckAnimationInfo(animations);

    // SkinnedData에 최종 전달
    skinnedInfo.Set(mBoneHierarchy, mBoneOffsets, animations);

    // SDK 객체 전부 해제
    // 이후 dx12쪽에서 렌더링은 SDK를 전혀 몰라도 됨.
    Cleanup();

    return true;
}

// SDK 초기화
bool FbxLoader::InitializeSDK(const std::string& filename)
{
    // FbxManager는 SDK 전체의 메모리 할당, 객체 생성 및 삭제하는 클래스
    mSdkManager = FbxManager::Create();
    if (mSdkManager == nullptr)
    {
        mLastError = "FbxManager 생성에 실패했습니다.";
        return false;
    }

    //임포트/익스포트 시 무엇을 읽고 쓸지 설정할 수 있는 트리구조의 데이터 타입
    //기본값은 전부 활성화 상태
    FbxIOSettings* ios = FbxIOSettings::Create(mSdkManager, IOSROOT); //IOSROOT -> 트리 루트 이름
    //지금은 스키닝 애니메이션이 목적이라 텍스쳐와 머테리얼은 비활성화
    ios->SetBoolProp(IMP_FBX_MATERIAL, false);
    ios->SetBoolProp(IMP_FBX_TEXTURE, false);
    mSdkManager->SetIOSettings(ios);

    //임포트된 데이터가 FbxScene에 담김
    mScene = FbxScene::Create(mSdkManager, "Scene");
    if (mScene == nullptr)
    {
        mLastError = "FbxScene 생성에 실패했습니다.";
        return false;
    }

    FbxImporter* importer = FbxImporter::Create(mSdkManager, "");

    // 두 번째 파라미터를 -1로 보내면 파일 확장자를 통해 포맷을 자동 판별해줌
    if (importer->Initialize(filename.c_str(), -1, mSdkManager->GetIOSettings()) == false)
    {
        mLastError = "파일 열기 실패: ";
        mLastError += importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }

    if (importer->Import(mScene) == false)
    {
        mLastError = "씬 임포트 실패: ";
        mLastError += importer->GetStatus().GetErrorString();
        importer->Destroy();
        return false;
    }

    // 데이터는 모두 mScene으로 복사해놨으므로 파일 핸들을 붙잡고 있을 이유가 없어서 해제함
    importer->Destroy();

    return true;
}


// 씬 전처리
void FbxLoader::PreprocessScene()
{
    // 좌표계 변환
	// 프로그램마다 좌표계가 다르므로, sdk가 제공하는 변환 api를 통해 좌표계를 변환해야 함
    // 그래도 최종 보정은 렌더 아이템의 월드 행렬에서 하는 편이 확실한듯
    FbxAxisSystem sceneAxis = mScene->GetGlobalSettings().GetAxisSystem();
    FbxAxisSystem dxAxis    = FbxAxisSystem::DirectX;

    //현재 씬의 좌표계와 directX 좌표계가 동일한지 체크
    if (sceneAxis != dxAxis)
    {
        dxAxis.ConvertScene(mScene);
    }

    // gpu는 삼각형만 그리는데 실제 fbx메쉬에는 다각형이 있을 수 있음
    // sdk 컨버터를 통해 삼각형으로 변환
    FbxGeometryConverter converter(mSdkManager);

    // 두번 째 인자는 원본 메시를 새 메시로 교체하는 지에 대한 설정
    // 교체 시 호출 이전에 얻어둔 FbxMesh* 는 무효가 됨
    // 그렇기에 반드시 FindFirstMesh()로 메쉬노드를 설정하기 전에 해줘야 함
    converter.Triangulate(mScene, true);
}

// 스켈레톤 상속 계층 생성
void FbxLoader::BuildSkeleton()
{
    // 혹시모를 더미 데이터 초기화
    mBoneHierarchy.clear();
    mBoneNodes.clear();
    mBoneNames.clear();
    mBoneIndexMap.clear();

    // GetRootNode()가 반환하는 노드는 파일에 실제로 존재하는 오브젝트가 아니라
    // SDK가 씬 구성을 위해 만들어 놓은 가상 컨테이너임
    // 어트리뷰트가 없음
    // 실제 오브젝트들이 여기에 자식으로 매달림
    FbxNode* root = mScene->GetRootNode();
    if (root == nullptr)
        return;

    for (int i = 0; i < root->GetChildCount(); ++i)
    {
        TraverseSkeleton(root->GetChild(i), -1);
    }
}


// DFS 전위 순회로 본에 인덱스를 부여
// 전위 순회는 자동으로 부모 먼저 찾아간 후 자식으로 가는 걸 보장하기 때문
void FbxLoader::TraverseSkeleton(FbxNode* node, int parentIndex)
{
    int myIndex = parentIndex;

    FbxNodeAttribute* attr = node->GetNodeAttribute();

    //본인 노드 발견 시 true
    const bool isBone = (attr != nullptr) &&
                        (attr->GetAttributeType() == FbxNodeAttribute::eSkeleton);

    if (isBone)
    {
        myIndex = static_cast<int>(mBoneHierarchy.size());

        mBoneHierarchy.push_back(parentIndex); // 부모 인덱스 저장
        mBoneNodes.push_back(node);
        mBoneNames.push_back(node->GetName() ? node->GetName() : "");
        mBoneIndexMap[node] = myIndex;
    }

    // 본이 아닌 노드를 만나면 본 관련 벡터에 추가하지 않고
    // 자식에게 원래 부모 인덱스를 그대로 물려줘서 본 사이의 계층을 유지
    for (int i = 0; i < node->GetChildCount(); ++i)
    {
        TraverseSkeleton(node->GetChild(i), myIndex);
    }
}

// 스킨 가중치 + 본 오프셋 행렬
// 가장 어려웠던 부분
bool FbxLoader::ExtractSkinWeights(FbxMesh* mesh)
{
    const int cpCount   = mesh->GetControlPointsCount();
    const int bCount = static_cast<int>(mBoneHierarchy.size());

    // 혹시 모를 더미 데이터 삭제 후 컨트롤 포인트 개수로 리사이즈
    mControlPointWeights.clear();
    mControlPointWeights.resize(cpCount);


    // 먼저 본 오프셋들을 항등행렬로 초기화하고
    // 클러스터가 있는 본만 덮어씌움(클러스터는 본 하나당 하나)
    // 클러스터가 없는 본은 항등이 그대로 남음
    // 어떤 정점도 그 본을 참조하지 않으므로 해당 본의 위치가
    // 정점의 최종 좌표를 계산할 때 곱해질 일이 없음
    // 그저 자식 본의 부모 계층을 이어 주는 역할
    XMFLOAT4X4 identity;
    XMStoreFloat4x4(&identity, XMMatrixIdentity());
    mBoneOffsets.assign(bCount, identity);

    const int deformerCount = mesh->GetDeformerCount(FbxDeformer::eSkin);
    // FbxDeformer는 메시를 변형시키는 기능을 하는데 FbxSkin는 그 하위 종류 중 하나
    // 그렇기에 이 디포머가 0이라면 스키닝이 되지 않는 메시
    if (deformerCount == 0)
    {
        mLastError = "스킨 디포머가 없습니다. 스키닝되지 않은 메시입니다.";
        return false;
    }

    // 어트리뷰트를 통해 노드 정보 가져옴
    FbxNode* meshNode = mesh->GetNode();
    // 해당 메쉬노드의 SRT를 뽑아서 행렬로 변환
    const FbxAMatrix geometryTransform = GetGeometryTransform(meshNode);

    // 스킨 하나에 여러 디포머가 있을 수 있음
    for (int d = 0; d < deformerCount; ++d)
    {
        // GetDeformer의 반환타입은 FbxDeformer이고 FbxSkin의 상위 타입임
        // FbxSkin의 기능을 사용하기 위해 다운캐스팅
        // 다운캐스팅을 안전하게 할 수 있는 이유는 eSkin으로 명시했기 때문
        FbxSkin* skin = static_cast<FbxSkin*>(mesh->GetDeformer(d, FbxDeformer::eSkin));
        if (skin == nullptr)
            continue;

        // 해당 FbxSkin에 클러스터가 몇 개인 지, 즉 몇 개의 본이 해당 메시에 영향을 주는 가를 구함
        const int clusterCount = skin->GetClusterCount();

        for (int c = 0; c < clusterCount; c++)
        {       
            FbxCluster* cluster = skin->GetCluster(c);
            if (cluster == nullptr)
                continue;

            // GetLink가 해당 클러스터가 붙은 본 노드 자체를 반환
            // 그래서 해당 클러스터는 어느 본에 붙어 있는 지 알 수 있음
            FbxNode* linkNode = cluster->GetLink();
            if (linkNode == nullptr)
                continue;

            // 정점 버퍼의 특정 원소에 접근하려면 정수(인덱스)가 필요
            // 하지만 fbx가 알려주는 건 FbxNode*이므로 포인터 -> 정수(인덱스용) 변환 필요
            // TraverseSkeleton()에서 mBoneIndexMap에다가 본노드에 대해 정수를 매핑했었음
            // find()로 linkNode의 해시값이 저장되어 있는 지 묻고 이터레이터를 반환
            const auto it = mBoneIndexMap.find(linkNode);
            // find로 못 찾으면 end와 같은 값을 반환 nullptr을 통해 찾지 못한 경우와 같은 의미
            if (it == mBoneIndexMap.end())
                continue;

            // 이터레이터는 pair<const FbxNode*, int>를 가리키고 second는 FbxNode*와 매핑된 int를 반환
            // 이를 통해 포인터 -> 정수로 변환 완료
            const int boneIndex = it->second;


            // 오프셋 행렬 구하는 부분
            //
            // 이게 필요한 이유는 정점은 메시 로컬 좌표계에 저장되어 있음
            // 여기서 본이 회전한다면 그 회전은 월드가 아닌 본의 로컬좌표 기준이므로
            // 본 회전에 의해 정점에도 회전을 적용하려면 우선 정점을 본 로컬 좌표로 변환해줘야 함
            // mBoneOffsets[i]는 바인드 포즈(보통 T포즈)시점에 메시 공간 -> i번 본 로컬 공간 변환을 의미함
            // 그래서 시간이 지나도 변하지 않기에 애니메이션과 무관하게 로드 시 한 번만 계산해도 됨
            //
            //   offset = (본 로컬 <- 월드) * (월드 <- 메시 로컬) * 지오메트리 오프셋
            //          = 메시 공간의 점을 그 본의 로컬 공간으로 옮기는 변환

            // 바인드 시점 메시의 월드 변환 (메시 로컬 -> 월드)
            FbxAMatrix transformMatrix;
            // 바인드 시점 본의 월드 변환   (본 로컬  -> 월드)
            FbxAMatrix transformLinkMatrix;

            cluster->GetTransformMatrix(transformMatrix);
            cluster->GetTransformLinkMatrix(transformLinkMatrix);

            // 행렬의 곱셈은 교환법칙이 성립하지 않기에 곱셈 순서에 주의해야함
            // FBX는 오른쪽 -> 왼쪽으로 곱셈함. DirecX는 왼 -> 오
            // * geometryTransform : 지오메트리 공간 -> 노드 로컬 
            // * transformMatrix   : 노드 로컬 -> 바인드 시점 월드
            // transformLinkMatrix.Inverse : 월드 -> 본 로컬
            // 그래서 '역'바인드 포즈 행렬
            const FbxAMatrix offset =
                transformLinkMatrix.Inverse() * transformMatrix * geometryTransform;

            // XmFloat4x4으로 변환 후 저장
            mBoneOffsets[boneIndex] = ConvertToXmFloat4x4(offset);


            // FBX가 저장하는 방향   : 본   -> 본이 영향을 주는 컨트롤 포인트(정점의 위치)들
            // dx12에 필요한  방향   : 정점 -> 정점에 영향을 주는 본들
            // 본과 정점에 대한 데이터를 역방향으로 변환해야 함

            // 한 클러스터가 몇 개의 컨트롤 포인트에 영향은 주는 지
            // cpIndices와 cpWeights의 길이이기도 함
            const int indexCount = cluster->GetControlPointIndicesCount();

            // SDK 내부 배열의 포인터를 그대로 받음 -> 즉 Destory() 후에 사라짐
            // 그래서 CleanUp() 호출 전에 mControlPointWeights로 옮기는 것
            // cpIndices는 클러스터에 영향을 cp들의 인덱스들
            // cpWeights은 클러스터에 영향을 받는 가중치들
            // 둘은 짝을 이룸
            int*    cpIndices = cluster->GetControlPointIndices();
            double* cpWeights = cluster->GetControlPointWeights();

            if (cpIndices == nullptr || cpWeights == nullptr)
                continue;

            for (int i = 0; i < indexCount; ++i)
            {
                //i 번째 쌍을 꺼냄
                const int   cpIndex = cpIndices[i];
                const float weight  = static_cast<float>(cpWeights[i]);

                // 인덱스 검증 cpCount는 실제 cp의 개수임
                if (cpIndex < 0 || cpIndex >= cpCount)
                    continue;   

                // 0 x n은 0이므로 저장할 이유가 없고 가중치가 음수라면 잘못된 거임
                if (weight <= 0.0f)
                    continue;   

                BoneWeight bw;
                bw.BoneIndex = boneIndex;
                bw.Weight    = weight;

                mControlPointWeights[cpIndex].push_back(bw);
            }
        }
    }

    NormalizeWeights();

    return true;
}


// 한 정점에 영향을 주는 본 가중치를 최대 4개로 자르고 합이 1이 되도록 정규화하는 함수
void FbxLoader::NormalizeWeights()
{
    for (auto& list : mControlPointWeights)
    {
        if (list.empty())
            continue;

        // 가중치 내림차순 정렬한 이유는 가중치가 큰 본이 날라가는 경우를 막기 위함
        // 람다식을 사용한 이유는 이 한 곳에서만 쓸려고 함수를 정의하기 귀찮았음
        std::sort(list.begin(), list.end(),
            [](const BoneWeight& a, const BoneWeight& b) -> bool
            {
                return a.Weight > b.Weight;
            });

        // 정점이 받을 수 있는 본의 개수가 최대 4개라 그거에 맞게 자름
        // 그래서 내림차순으로 정리하는 것
        if (list.size() > 4)
            list.resize(4);


        float sum = 0.0f;
        // 자르면서 원본의 가중치 합이 깨짐
        // 셰이더에서 가중치의 마지막 인덱스를 1에서 그전 가중치들의 합을 빼는 것으로 정해놨음
        // 그래서 총합 분의 각 요소를 나눠 합계 1.0이 되도록 정규화
        for (const auto& bw : list)
            sum += bw.Weight;

        // 0 나누기 방지
        if (sum > 1e-6f)
        {
            for (auto& bw : list)
                bw.Weight /= sum;
        }
        else
        {
            // 가중치가 전부 0에 가까운 이상 데이터 — 첫 본에 전부 몰아줌
            list.resize(1);
            list[0].Weight = 1.0f;
        }
    }
}


// 메쉬의 폴리곤들을 GPU의 정점 배열에 맞게 세팅하는 함수
bool FbxLoader::ExtractMesh(FbxMesh* mesh,
                            std::vector<SkinnedVertex>& vertices,
                            std::vector<std::uint32_t>& indices)
{

    const int   polygonCount  = mesh->GetPolygonCount();
    FbxVector4* controlPoints = mesh->GetControlPoints();

    if (polygonCount == 0 || controlPoints == nullptr)
    {
        mLastError = "메시에 폴리곤이 없습니다.";
        return false;
    }

    vertices.clear();
    indices.clear();
    // capacity만 미리 확보하고 size는 0으로 설정
    // vector가 커지며 resize가 자동으로 여러 번 일어나는데 정점의 수가 많아질수록 굉장히 용량이 커짐
    vertices.reserve(polygonCount * 3);
    indices.reserve(polygonCount * 3);

    for (int p = 0; p < polygonCount; ++p)
    {
        // Triangulate를 거쳤으므로 항상 3이어야 함
        if (mesh->GetPolygonSize(p) != 3)
        {
            mLastError = "삼각형이 아닌 폴리곤이 남아 있습니다.";
            return false;
        }

        for (int v = 0; v < 3; ++v) // 폴리곤 정점의 슬롯 당
        {
            //p 폴리곤의 v번째 슬롯에 있는 cp의 인덱스를 저장
            const int cpIndex = mesh->GetPolygonVertex(p, v);
            if (cpIndex < 0)
            {
                mLastError = "잘못된 컨트롤 포인트 인덱스입니다.";
                return false;
            }
          
            SkinnedVertex vertex = {};

            // 위치값 변환 후 세팅
            const FbxVector4& cp = controlPoints[cpIndex];
            vertex.Pos.x = static_cast<float>(cp[0]);
            vertex.Pos.y = static_cast<float>(cp[1]);
            vertex.Pos.z = static_cast<float>(cp[2]);

            // 노멀값 세팅
            if (ReadNormal(mesh, cpIndex, (p * 3 + v), vertex.Normal) == false)
            {
                //실패 시 업벡터로 세팅
                vertex.Normal = XMFLOAT3(0.0f, 1.0f, 0.0f);
            }
       
            // uv값 세팅
            if (ReadUV(mesh, cpIndex, p, v, vertex.TexC) == false)
            {
                //실패 시 기본 원점 세팅
                vertex.TexC = XMFLOAT2(0.0f, 0.0f);
            }

            // 스킨 가중치는 컨트롤 포인트에 붙어 있으므로 cpIndex로 조회
            FillSkinData(cpIndex, vertex);

            vertices.push_back(vertex);
            // vertices.size() - 1은 위에 넣은 정점의 인덱스
            // LoadSkinnedModel에서 요구하는 indices자료향에 맞게 uint32로 캐스팅
            indices.push_back(static_cast<std::uint32_t>(vertices.size() - 1));

        }
    }

    return true;
}


// 컨트롤 포인트별 가중치 목록을 정점에 채움
// 기본값 (0,0,0) / 인덱스 전부 0 은 셰이더에서
// weights[3] = 1 - 0 = 1.0 이 되어 루트 본을 따라가게 됨
void FbxLoader::FillSkinData(int cpIndex, SkinnedVertex& vertex) const
{
    // 기본값 세팅
    // 가중치와 인덱스 전부 0 은 셰이더에서
    // weights[3] = 1 - 0 = 1.0 이 되어 루트 본을 따라가게 됨
    vertex.BoneWeights    = XMFLOAT3(0.0f, 0.0f, 0.0f);
    vertex.BoneIndices[0] = 0;
    vertex.BoneIndices[1] = 0;
    vertex.BoneIndices[2] = 0;
    vertex.BoneIndices[3] = 0;

    // 배열 범위 검증
    // size()가 size_t를 반환하는데 int와 비교하면 int가 size_t로 바뀌며 값이 이상하게 바뀔 수 있음
    // 그래서 int로 캐스팅
    if (cpIndex < 0 || cpIndex >= static_cast<int>(mControlPointWeights.size()))
        return;

    const auto& list = mControlPointWeights[cpIndex];
    // 어떤 본에도 연결되지 않은 정점
    if (list.empty())
        return;   

    // 이미 내림차순 정렬, 4개 이하, 합 == 1로 정규화가 끝난 검증된 상태
    const int count = static_cast<int>(list.size());

    for (int i = 0; i < count; ++i)
    {
        // 그래서 여기서 별도의 범위 검증이 필요 없음
        // BoneIndices에 맞게 캐스팅
        vertex.BoneIndices[i] = static_cast<std::uint8_t>(list[i].BoneIndex);
    }

    // 가중치는 앞의 3개만 저장해도 됨 4번째는 셰이더가 1 - (x+y+z)로 계산함
    if (count > 0) vertex.BoneWeights.x = list[0].Weight;
    if (count > 1) vertex.BoneWeights.y = list[1].Weight;
    if (count > 2) vertex.BoneWeights.z = list[2].Weight;
}

// 어떤 스택을 클립으로 삼을 지 결정하는 함수
bool FbxLoader::ExtractAnimations(
    std::unordered_map<std::string, AnimationClip>& animations)
{
    animations.clear();
    mClipNames.clear();

    //FbxAnimStack -> 클립 하나
    const int stackCount = mScene->GetSrcObjectCount<FbxAnimStack>();
    if (stackCount == 0)
    {
        mLastError = "애니메이션 스택이 없습니다.";
        return false;
    }

    for (int i = 0; i < stackCount; ++i)
    {
        FbxAnimStack* animStack = mScene->GetSrcObject<FbxAnimStack>(i);
        if (animStack == nullptr)
            continue;

        const std::string clipName = ( animStack->GetName() )? animStack->GetName() : "";
        if (clipName.empty())
            continue;

        // mixamo는 커브가 하나도 없는 빈 스택을 함께 내보내느데
        // 그걸 샘플링하면 EvaluateLocalTransform()에서 바인드 포즈만 반환해서
        // 모든 키프레임이 동일한 값이 되어 캐릭터가 t포즈로 멈춰있음
        // 그래서 커브가 없는 빈 스택은 건너뜀
        if (HasAnimationCurves(animStack) == false)
            continue;

        AnimationClip clip;
        SampleClip(animStack, clip);

        // SkinnedData::GetFinalTransforms()에서 BoneAnimations을 본 개수만큼 순회함
        // 만약 본 개수가 부족하면 SkinnedData가 초기화되지 데이터까지 읽어버림
        // 그러면 캐릭터가 뒤틀리는 증상이 발생할 수 있음
        if (clip.BoneAnimations.size() != mBoneHierarchy.size())
            continue;

        //SkinnedData가 이름으로 클립을 조회하는 맵
        animations[clipName] = clip;
        mClipNames.push_back(clipName);
    }

    // 스택은 있지만 전부 걸러진 경우
    if (animations.empty())
    {
        mLastError = "유효한 애니메이션 클립이 없습니다.";
        return false;
    }

    return true;
}

//---------------------------------------------------------------------------------------
// 클립 하나를 균일 간격으로 샘플링한다.
//
// FBX 커브는 본마다 키 시각이 제각각이지만, AnimationClip::Interpolate()는
// 모든 본을 같은 시각 t로 조회하는 구조다. 고정 간격으로 미리 뽑아두면
// 이 구조에 정확히 맞고, 커브가 아예 없는 본도 바인드 포즈 값으로 자동으로 채워진다.
//---------------------------------------------------------------------------------------
void FbxLoader::SampleClip(FbxAnimStack* animStack, AnimationClip& clip)
{
    // 씬에 현재 어떤 클립을 기준으로 할 것인지 명시해줌
    // 빠뜨리면 모든 클립이 첫 번째 것으로 평가돼 전부 똑같이 나옴
    mScene->SetCurrentAnimationStack(animStack);

    // 클립이 몇 초부터 몇 초까지인지 구함
    // 시간을 부동소수점으로 누적하면 안 됨 유한비트라 어디선가 수가 손실됨 -> 오차가 쌓여 어긋날 수 있음
    // FbxTimeSpan 내부적으로 틱을 정수로 저장함 ex) 1초에 약 460억 틱 -> 오차 x

    const FbxTimeSpan timeSpan = animStack->GetLocalTimeSpan();
    const double startSec = timeSpan.GetStart().GetSecondDouble();
          double endSec   = timeSpan.GetStop().GetSecondDouble();

    // 길이가 0이거나 음수인 비정상 클립이 들어오면 밑에 계산이 전부 0이 되버리니 
    // 최소한의 프레임을 갖도록 강제함
    if (endSec <= startSec)
        endSec = startSec + mSampleRate;

    const double duration = endSec - startSec;

    // 몇 칸으로 나눌 지에 대한 부분
    // +1을 하는 이유는 구간과 점의 개수가 다르기때문  ex)  ._._.   -> 점 3개 구간 2개
    // 나눗셈은 구간을 세고 거기에 +1을 통해 점의 개수를 구함
    int sampleCount = static_cast<int>(duration / mSampleRate) + 1;

    // 칸이 하나면 보간을 할 수 없고 밑에 sampleCount - 1을 만나면 0 나누기가 되버리니 최소 2개로 강제
    if (sampleCount < 2)
        sampleCount = 2;

    // 구간을 균등하게 분할하기 위한 연산식
    const double step = duration / (sampleCount - 1);

    const int boneCount = static_cast<int>(mBoneHierarchy.size());
    clip.BoneAnimations.resize(boneCount);

    // 행 - 본, 열 - 시각
    // 행 우선 순회로 본 하나랄 정하고 그 본의 전 시간대를 채운 뒤 다음 본으로 넘어감
    for (int b = 0; b < boneCount; b++)
    {
        FbxNode* boneNode = mBoneNodes[b];

        BoneAnimation& boneAnim = clip.BoneAnimations[b];
        // reserve아니고 resize인 이유
        // Keyframes[s]로 바로 접근하는데 reserve는 용량만 잡고 size는 0이라 [s] 접근이 범위 밖임
        boneAnim.Keyframes.resize(sampleCount);

        for (int s = 0; s < sampleCount; s++)
        {
            // s번 째 점의 시각
            // t += step 누적하면 부동소수 오차가 쌓여 마지막이 안 맞을 수도 있음
            // 곱셈은 오차가 쌓이지 않기에 곱셈을 사용
            const double t = startSec + s * step;

            //SDK가 요구하는 FbxTime 타입으로 변환
            FbxTime fbxTime;
            fbxTime.SetSecondDouble(t);

            // 이 본은 이 시각에 어떤 자세인가?
            // PreRotation / PostRotation - 조인트 방향
            // 애니메이션 커브 - 그 시각의 보간된 값
            // 회전 순서 — XYZ냐 ZYX냐. 다르면 결과가 완전히 달라짐
            // 피벗 — 회전/스케일 중심점
            // SDK가 위 사항들을 모두 반영한 최종 로컬 변환 반환
            const FbxAMatrix localTransform = boneNode->EvaluateLocalTransform(fbxTime);

            Keyframe& kf = boneAnim.Keyframes[s];

            // 클립 시작을 0으로 맞춘 상대 시각
            // SkinnedModelInstance가 루프할 때 TimePos를 0으로 되돌리기 때문
            kf.TimePos = static_cast<float>(t - startSec);

            // 회전 행렬을 원소별로 선형 보간하면 직교성이 깨짐
            // 회전은 반드시 쿼터니언 SLERP로 보간해야 구면을 따라 자연스럽게 회전함
            // FbxAMatrix는 내부에 SRT 성분을 갖고 있어 분해 없이 바로 꺼낼 수 있음
            const FbxVector4    T = localTransform.GetT();  
            const FbxVector4    S = localTransform.GetS();
            const FbxQuaternion Q = localTransform.GetQ();

            // 값 세팅
            kf.Translation.x = static_cast<float>(T[0]);
            kf.Translation.y = static_cast<float>(T[1]);
            kf.Translation.z = static_cast<float>(T[2]);

            kf.Scale.x = static_cast<float>(S[0]);
            kf.Scale.y = static_cast<float>(S[1]);
            kf.Scale.z = static_cast<float>(S[2]);

            kf.RotationQuat.x = static_cast<float>(Q[0]);
            kf.RotationQuat.y = static_cast<float>(Q[1]);
            kf.RotationQuat.z = static_cast<float>(Q[2]);
            kf.RotationQuat.w = static_cast<float>(Q[3]);
        }
    }
}



// 이 스택에 실제 애니메이션 커브가 있는지 확인
// mixamo 등의 익스포터는 커브가 전혀 없는 빈 스택을 함께 내보내는 경우가 있음
// 그런 스택을 샘플링하면 EvaluateLocalTransform()이 바인드 포즈만 반환해서
// 애니메이션이 재생되지 않는 것처럼 보임
bool FbxLoader::HasAnimationCurves(FbxAnimStack* animStack) const
{
    const int layerCount = animStack->GetMemberCount<FbxAnimLayer>();

    for (int l = 0; l < layerCount; ++l)
    {
        FbxAnimLayer* layer = animStack->GetMember<FbxAnimLayer>(l);
        if (layer == nullptr)
            continue;

        for (FbxNode* node : mBoneNodes)
        {
            if (node->LclRotation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X) != nullptr ||
                node->LclTranslation.GetCurve(layer, FBXSDK_CURVENODE_COMPONENT_X) != nullptr)
            {
                return true;
            }
        }
    }

    return false;
}

//스켈레톤의 본 개수와 비정상적인 계층 구조를 가진 본의 개수 체크
void FbxLoader::CheckSkeletonInfo() const
{
    if (!mVerbose)
        return;

    int violation = 0;
    for (size_t i = 0; i < mBoneHierarchy.size(); ++i)
    {
        if (mBoneHierarchy[i] >= static_cast<int>(i))
            ++violation;
    }

    std::ostringstream oss;
    oss << "[스켈레톤] 본 " << mBoneHierarchy.size() << "개, "
        << "계층 위반 " << violation << "개\n";

    OutputDebugStringA(oss.str().c_str());
}


//스킨의 cp개수와 스키닝이 된 것과 안 된 스킨의 개수 체크
void FbxLoader::CheckSkinInfo() const
{
    if (!mVerbose)
        return;

    int skinned = 0;
    int empty = 0;

    for (const auto& list : mControlPointWeights)
    {
        if (list.empty()) 
            ++empty;
        else              
            ++skinned;
    }

    std::ostringstream oss;
    oss << "[스킨] 컨트롤포인트 " << mControlPointWeights.size()
        << " (스키닝 o " << skinned << " / 스키닝 x " << empty << ")";


    OutputDebugStringA(oss.str().c_str());
}

//메쉬에 대한 정보 체크
void FbxLoader::CheckMeshInfo(FbxMesh* mesh,
                             const std::vector<SkinnedVertex>& vertices,
                             const std::vector<std::uint32_t>& indices) const
{
    if (!mVerbose)
        return;

    std::ostringstream oss;
    oss << "[메시]\n"
        << "정점 " << vertices.size() << "개\n"
        << "삼각형 " << indices.size() / 3 << "개\n"
        << "노멀 " << (mesh->GetElementNormalCount() > 0 ? "있음" : "없음") << "\n"
        << "UV " << (mesh->GetElementUVCount() > 0 ? "있음" : "없음") << "\n";

    OutputDebugStringA(oss.str().c_str());
}

// 애니메이션 클립 수와 정보 체크
void FbxLoader::CheckAnimationInfo(
    const std::unordered_map<std::string, AnimationClip>& animations) const
{
    if (!mVerbose)
        return;

    std::ostringstream oss;
    oss << "[애니메이션] 클립 개수 : " << animations.size();

    for (const auto& pair : animations)
    {
        const std::string& clipName = pair.first;
        const AnimationClip& clip = pair.second;

        const size_t kFrameCount =
            ( clip.BoneAnimations.empty() ) ? 0 : clip.BoneAnimations[0].Keyframes.size();

        oss << "[" << clipName << "]\n"
            << "본 " << clip.BoneAnimations.size() << "개\n"
            << "키프레임 " << kFrameCount << "개\n"
            << "길이 " << clip.GetClipEndTime() << "초\n";
    }

    OutputDebugStringA(oss.str().c_str());

}
