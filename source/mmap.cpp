#include "mmap.h"
#include "tinyxml2.h"

using namespace tinyxml2;

u32 getXmlUnsignedInt(XMLElement* el)
{
	if(!el) return 0;

	const char* str = el->GetText();
	if(!str) return 0;

	return strtoul(str, NULL, 0);
}

u32 getXmlInt(XMLElement* el)
{
	if(!el) return 0;

	const char* str = el->GetText();
	if(!str) return 0;

	return strtol(str, NULL, 0);
}

// TODO : error checking
memorymap_t* loadMemoryMap(char* path)
{
	if(!path)return NULL;

    XMLDocument doc;
    if(doc.LoadFile(path))return NULL;

	memorymap_t* ret = (memorymap_t*) malloc(sizeof(memorymap_t));
	if(!ret) return NULL;

    XMLElement* header_element = doc.FirstChildElement("header");
	if(header_element)
	{
		ret->num = getXmlUnsignedInt(header_element->FirstChildElement("num"));
		ret->processLinearOffset = getXmlUnsignedInt(header_element->FirstChildElement("processLinearOffset"));
	}else return NULL;

    XMLElement* map = doc.FirstChildElement("map");
    if(map)
    {
		int i = 0;

		for (tinyxml2::XMLElement* child = map->FirstChildElement(); child != NULL && i < ret->num; child = child->NextSiblingElement())
		{
			if(!strcmp(child->Name(), "entry"))
			{
				ret->map[i].src = getXmlInt(child->FirstChildElement("src"));
				ret->map[i].dst = getXmlInt(child->FirstChildElement("dst"));
				ret->map[i].size = getXmlInt(child->FirstChildElement("size"));

				i++;
			}
		}

		if(i == ret->num) return ret;
    }

    free(ret);

    return NULL;
}

memorymap_t* loadMemoryMapTitle(u32 tidlow, u32 tidhigh)
{
	static char path[256];
	snprintf(path, 255, "sdmc:/mmap/%08X%08X.xml", (unsigned int)tidhigh, (unsigned int)tidlow);
	return loadMemoryMap(path);
}
