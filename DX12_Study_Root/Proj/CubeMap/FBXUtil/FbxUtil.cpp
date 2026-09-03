#include "FbxUtil.h"
#include <fbxsdk.h>

namespace FbxUtil
{

	//FbxAMatrix는 double기반이라 float로 형변환 필요
	DirectX::XMFLOAT4X4 ConvertToXmFloat4x4(const FbxAMatrix& m)
	{
		DirectX::XMFLOAT4X4 result;

		for (int r = 0; r < 4; r++)
		{
			for (int c = 0; c < 4; c++)
			{
				result.m[r][c] = static_cast<float>(m.Get(r, c));
			}
		}

		return result;
	}

	// 노드 계층에 포함되지 않는 지오메트리 오프셋 추출하는 함수
	FbxAMatrix GetGeometryTransform(FbxNode* node)
	{
		//node의 S R T를 추출하여 하나의 FbxAMatrix로 변환
		//eSourcePivot -> 원본 파일에 기록된 피벗 설정을 그대로 유지
		const FbxVector4 T = node->GetGeometricTranslation(FbxNode::eSourcePivot); 
		const FbxVector4 R = node->GetGeometricRotation(FbxNode::eSourcePivot);
		const FbxVector4 S = node->GetGeometricScaling(FbxNode::eSourcePivot);

		return FbxAMatrix(T, R, S);
	}

	// 노드트리 순회하며 속성이 eMesh인 첫 번째 메시를 찾는 함수
	// FbxNode노드트리를 재귀 + DFS로 순회
	// 현재는 단일메시 파일을 읽어서 상관 없지만 파츠가 여러 개인 여러 메시를 보유한
	// 모델일 경우 나머지가 날라가므로 그땐 submesh 개념에 대한 구현이 필요함
	FbxMesh* FindFirstMesh(FbxNode* node)
	{
		if (node == nullptr)
			return nullptr;

		FbxNodeAttribute* attr = node->GetNodeAttribute();

		// 발견
		if (attr != nullptr &&
			attr->GetAttributeType() == FbxNodeAttribute::eMesh)
		{
			//이미 조건문에서 mesh임을 확인했으니 안전하게 cast 가능
			return static_cast<FbxMesh*>(attr);
		}

		for (int i = 0; i < node->GetChildCount(); i++)
		{
										   //DFS
			FbxMesh* found = FindFirstMesh(node->GetChild(i));

			if (found != nullptr)
				return found;
		}

		//찾지 못했을 경우 nullptr반환
		return nullptr;
	}

	// 매핑, 레퍼런스 모드 조합을 처리하여 노멀을 읽는 함수
	// eByControlPoint  : 컨트롤 포인트당 하나 
	// eByPolygonVertex : 폴리곤 정점당 하나  
	// eDirect          : 값 배열에서 직접 조회
	// eIndexToDirect   : 인덱스 배열을 한 번 거쳐서 조회
	// GPU 버텍스 버퍼에는 이런 구분이 없이 정점 하나에 노멀 하나이므로,
	// 모든 조합을 폴리곤 정점 기준으로 변환함
	bool ReadNormal(FbxMesh* mesh, int cpIndex, int vertexCounter, DirectX::XMFLOAT3& out)
	{
		//노멀 속성이 없다면 탈출
		if (mesh->GetElementNormalCount() < 1)
			return false;

		FbxGeometryElementNormal* eleNormal = mesh->GetElementNormal(0);

		int index = -1;

		// fbxsdk는 2가지의 매핑 모드와 2가지의 레퍼런스 모드 조합해 총 4가지의 조합을 만들기에
		// 이중 switch문을 통해 각 조합들의 경우의 수를 처리함
		// 외곽 switch - 매핑 모드
		// 내부 switch - 레퍼런스 모드
		switch (eleNormal->GetMappingMode())
		{
		case FbxGeometryElement::eByControlPoint:

			switch (eleNormal->GetReferenceMode())
			{
				case FbxGeometryElement::eDirect:
					index = cpIndex;
					break;

				case FbxGeometryElement::eIndexToDirect:
					index = eleNormal->GetIndexArray().GetAt(cpIndex);
					break;

				default:
					return false;
			}
			break;

		case FbxGeometryElement::eByPolygonVertex:

			switch (eleNormal->GetReferenceMode())
			{
				case FbxGeometryElement::eDirect:
					index = vertexCounter;
					break;

				case FbxGeometryElement::eIndexToDirect:
					index = eleNormal->GetIndexArray().GetAt(vertexCounter);
					break;

				default:
					return false;
			}
			break;

		default:
			return false;
		}

		//인덱스 범위 검증
		if (index < 0 || 
			index >= eleNormal->GetDirectArray().GetCount())
			return false;

		//XMFLOAT3으로 변환
		const FbxVector4 n = eleNormal->GetDirectArray().GetAt(index);
		out.x = static_cast<float>(n[0]);
		out.y = static_cast<float>(n[1]);
		out.z = static_cast<float>(n[2]);

		return true;
	}

	// 매핑, 레퍼런스 모드 조합을 처리해서 uv를 읽는 함수 
	// 노멀과 같은 구조지만 eByPolygonVertex인 경우 SDK가 제공하는
	// GetTextureUVIndex()가 레퍼런스 모드까지 내부에서 함께 처리해줘서 
	// eDirect/eIndexToDirect를 구분할 필요가 없음
	// FBX는 uv좌표계가 원점이 좌하단이고 DirectX는 좌상단이므로 V를 뒤집어야 함
	bool ReadUV(FbxMesh* mesh, int cpIndex, int polygonIndex, int positionInPolygon,
		DirectX::XMFLOAT2& out)
	{
		//UV속성이 없다면 탈출
		if (mesh->GetElementUVCount() < 1)
			return false;

		FbxGeometryElementUV* eleUV = mesh->GetElementUV(0);

		int index = -1;

		switch (eleUV->GetMappingMode())
		{
		case FbxGeometryElement::eByControlPoint:

			switch (eleUV->GetReferenceMode())
			{
				case FbxGeometryElement::eDirect:
					index = cpIndex;
					break;

				case FbxGeometryElement::eIndexToDirect:
					index = eleUV->GetIndexArray().GetAt(cpIndex);
					break;

				default:
					return false;
			}
			break;

		case FbxGeometryElement::eByPolygonVertex:
			// GetTextureUVIndex()가 레퍼런스 모드까지 내부에서 처리해줌
		    // 노멀에는 대응하는 편의 함수가 없어 ReadNormal은 직접 분기했음
			index = mesh->GetTextureUVIndex(polygonIndex, positionInPolygon);
			break;

		default:
			return false;
		}

		//인덱스 범위 검증
		if (index < 0 ||
			index >= eleUV->GetDirectArray().GetCount())
			return false;

		//XMFLOAT2으로 변환
		const FbxVector2 uv = eleUV->GetDirectArray().GetAt(index);

		out.x = static_cast<float>(uv[0]);
		//v 뒤집기
		out.y = 1.0f - static_cast<float>(uv[1]); 

		return true;
	}
}