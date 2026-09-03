#pragma once

#include <string>
#include <DirectXMath.h>

// 헤더에서 fbxsdk를 include 하지 않고
// 필요한 타입만 전방 선언하여 하고 cpp에서 include
namespace fbxsdk
{
	class FbxNode;
	class FbxMesh;
	class FbxAMatrix;
}

using fbxsdk::FbxNode;
using fbxsdk::FbxMesh;
using fbxsdk::FbxAMatrix;

// 원래는 FbxLoader의 함수들이었지만
// FbxLoader의 멤버변수를 갖거나 변화를 주지 않고, 단순 기능만 제공하는 함수들은
// FbxLoader를 좀 더 가볍게 만들어 가시성을 높이기 위해 
// 외부로 빼서 FbxUtil 네임스페이스로 묶음
// 처음엔 FbxUtil이라는 클래스로 만들려고 했는데
// 그럼 Loader가 이 클래스를 또 멤버로 갖고 있어야 하고
// 유틸함수 사용시 loader->FbxUtil->ConvertToXmFloat4x4 이런식으로 접근해야 해서
// 가시성만 더 떨어진다고 판단하여 네임스페이스로 결정함
namespace FbxUtil
{
	//FbxAMatrix는 double기반이라 float로 형변환 필요
	DirectX::XMFLOAT4X4 ConvertToXmFloat4x4(const FbxAMatrix & m);

	// 노드 계층에 포함되지 않는 지오메트리 오프셋 추출하는 함수
	FbxAMatrix GetGeometryTransform(FbxNode* node);

	// 노드트리 순회하며 첫 번째 메시를 찾는 함수
	FbxMesh* FindFirstMesh(FbxNode* node);

	// 매핑, 레퍼런스 모드 조합을 처리하여 노멀을 읽는 함수
	// fbxVector의 요소들도 double이라 float으로 변환해야함
	bool ReadNormal(FbxMesh* mesh, int cpIndex, int vertexCounter, DirectX::XMFLOAT3& out);

	// 매핑, 레퍼런스 모드 조합을 처리해서 uv를 읽는 함수
	// fbxVector의 요소들도 double이라 float으로 변환해야함
	bool ReadUV(FbxMesh* mesh, int cpIndex, int polygonIndex, int positionInPolygon,
		DirectX::XMFLOAT2& out);

}