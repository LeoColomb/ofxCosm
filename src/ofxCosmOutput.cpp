#include "ofxCosmOutput.h"

#include "Poco/DOM/DOMParser.h"
#include "Poco/DOM/Document.h"
#include "Poco/DOM/NodeList.h"
#include "Poco/DOM/NodeIterator.h"
#include "Poco/DOM/NodeFilter.h"
#include "Poco/DOM/AutoPtr.h"
#include "Poco/SAX/InputSource.h"
#include "Poco/DOM/AttrMap.h"

#include "Poco/Exception.h"

#include <fstream>

ofxCosmOutput::ofxCosmOutput(bool _bThreaded)
	: ofxCosmFeed(_bThreaded)
{
	ofAddListener(responseEvent, this, &ofxCosmOutput::onResponse);

	fLastOutput = ofGetElapsedTimef();
}

ofxCosmOutput::~ofxCosmOutput()
{
}

bool
	ofxCosmOutput::output(int _format, bool _force)
{
	if (ofGetElapsedTimef() - fLastOutput < fMinInterval && !_force)
		return false;

	if (bThreaded && bRequestQueued)
		return false;

	if (sApiKey == "" || iFeedId == -1)
	{
		bLastRequestOk = false;
		return false;
	}

	if (_format == OFX_COSM_CSV)
	{
		request.method = OFX_COSM_GET;
		request.format = OFX_COSM_CSV;
		request.clearHeaders();
		request.addHeader("X-CosmApiKey", sApiKey);
		char pcUrl[256];
		sprintf(pcUrl, "%s%d.csv", sApiUrl.c_str(), iFeedId);
		request.url = pcUrl;
		request.timeout = 5;
	}
	else if (_format == OFX_COSM_EEML)
	{
		request.method = OFX_COSM_GET;
		request.format = OFX_COSM_EEML;
		request.clearHeaders();
		request.addHeader("X-CosmApiKey", sApiKey);
		char pcUrl[256];
		sprintf(pcUrl, "%s%d.xml", sApiUrl.c_str(), iFeedId);
		request.url = pcUrl;
		request.timeout = 5;
	}
	else
	{
		/// unrecognized format
		return false;
	}

	fLastOutput = ofGetElapsedTimef();

	if (bThreaded)
		bRequestQueued = true;
	else
		sendRequest(request);
	return true;
}

bool
	ofxCosmOutput::parseResponseCsv(string _response)
{
	bool bEOL = false;
	int i = 0;
	while (!bEOL)
	{
		int iPos = _response.find_first_of(",");
		bEOL = iPos < 0;

		if (pData.size() <= i)
		{
			ofxCosmData d;
			d.iId = i;
			pData.push_back(d);
		}

		ofxCosmData& data = pData.at(i);
		std::string sValue = _response.substr(0, iPos);
		while (sValue.at(0) == ' ')
			sValue = sValue.substr(1);
		data.fValue = atof(sValue.c_str());
		_response = _response.substr(iPos+1);

		++i;
	}

	return true;
}

bool
	ofxCosmOutput::parseResponseEeml(string _response)
{
	if (bVerbose) printf("[COSM] start parsing eeml\n");
	try
	{
		pData.clear();
		Poco::XML::DOMParser parser;
		Poco::XML::AttrMap* pMap;
		Poco::XML::AutoPtr<Poco::XML::Document> pDoc = parser.parseMemory(_response.c_str(), _response.length());

		Poco::XML::NodeIterator itElem(pDoc, Poco::XML::NodeFilter::SHOW_ELEMENT);

		Poco::XML::Node* pNode = itElem.nextNode();
		while (pNode)
		{
			if (pNode->nodeName() == Poco::XML::XMLString("environment"))
			{
				pMap = (Poco::XML::AttrMap*)pNode->attributes();
				sUpdated = pMap->getNamedItem("updated")->nodeValue();
			}

			if (pNode->nodeName() == Poco::XML::XMLString("title"))
				sTitle = pNode->firstChild()->getNodeValue();
			if (pNode->nodeName() == Poco::XML::XMLString("status"))
				sStatus = pNode->firstChild()->getNodeValue();
			if (pNode->nodeName() == Poco::XML::XMLString("description"))
				sDescription = pNode->firstChild()->getNodeValue();
			if (pNode->nodeName() == Poco::XML::XMLString("website"))
				sWebsite = pNode->firstChild()->getNodeValue();

			if (pNode->nodeName() == Poco::XML::XMLString("location"))
			{
				//				pMap = (Poco::XML::AttrMap*)pNode->attributes();
				//				location.sDomain = pMap->getNamedItem("domain")->nodeValue();
				//				location.sExposure = pMap->getNamedItem("exposure")->nodeValue();
				//				location.sDisposition = pMap->getNamedItem("disposition")->nodeValue();

				Poco::XML::NodeIterator itChildren(pNode, Poco::XML::NodeFilter::SHOW_ELEMENT);
				Poco::XML::Node* pChild = itChildren.nextNode();
				while (pChild)
				{
					if (pChild->nodeName() == Poco::XML::XMLString("name"))
						location.sName = pChild->firstChild()->nodeValue();
					if (pChild->nodeName() == Poco::XML::XMLString("lat"))
						location.sLat = pChild->firstChild()->nodeValue();
					if (pChild->nodeName() == Poco::XML::XMLString("lon"))
						location.sLon = pChild->firstChild()->nodeValue();

					pChild = itChildren.nextNode();
				}
			}

			if (pNode->nodeName() == Poco::XML::XMLString("data"))
			{
				ofxCosmData data;

				pMap = (Poco::XML::AttrMap*)pNode->attributes();
				data.iId = atoi(pMap->getNamedItem("id")->nodeValue().c_str());

				Poco::XML::NodeIterator itChildren(pNode, Poco::XML::NodeFilter::SHOW_ELEMENT);
				Poco::XML::Node* pChild = itChildren.nextNode();
				while (pChild)
				{
					if (pChild->nodeName() == Poco::XML::XMLString("tag"))
						data.pTags.push_back(pChild->firstChild()->getNodeValue());

					if (pChild->nodeName() == Poco::XML::XMLString("value"))
					{
						data.fValue = atof(pChild->firstChild()->getNodeValue().c_str());

						pMap = (Poco::XML::AttrMap*)pChild->attributes();
						data.fValueMin = atof(pMap->getNamedItem("minValue")->nodeValue().c_str());
						data.fValueMax = atof(pMap->getNamedItem("maxValue")->nodeValue().c_str());
					}

					pChild = itChildren.nextNode();
				}

				pData.push_back(data);
			}

			pNode = itElem.nextNode();
		}
	}
	catch (Poco::Exception& exc)
	{
		printf("[COSM] Parse xml exception: %s\n", exc.displayText().c_str());
		return false;
	}
	if (bVerbose) printf("[COSM] finished parsing eeml\n");

	return true;
}

void
	ofxCosmOutput::onResponse(ofxCosmResponse &response)
{
	if (bVerbose)
	{
		printf("[COSM] received response with status %d\n", response.status);
		printf("[COSM] %s\n", response.reasonForStatus.c_str());
		printf("[COSM] %s\n", response.responseBody.c_str());
	}

	if (response.status == 200)
	{
		bool bParsedOk;
		if (response.format == OFX_COSM_CSV)
			bParsedOk = parseResponseCsv(response.responseBody);
		else if (response.format == OFX_COSM_EEML)
			bParsedOk = parseResponseEeml(response.responseBody);

		if (bParsedOk)
		{
			bLastRequestOk = true;
			fLastResponseTime = ofGetElapsedTimef();
		}
		else
		{
			bLastRequestOk = false;
		}
	}
	else
	{
		bLastRequestOk = false;
		printf("[COSM] Error: response failed with status %d\n", response.status);
		printf("[COSM] %s\n", response.responseBody.c_str());
	}
}