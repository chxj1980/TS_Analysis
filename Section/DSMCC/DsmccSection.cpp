#include "DsmccSection.h"
#include "ObjDir.h"
#include "ObjFile.h"
#include "../SectionFactory.h"

DsmccSection::DsmccSection()
{

}

DsmccSection::DsmccSection(uint8_t* data, uint16_t len, uint32_t crc /* = 0xFFFFFFFF */)
    : Section(data, len, crc),
      table_id_extension((data[3] << 8) | data[4]),
      version_number((data[5] >> 1) & 0x1F),
      current_next_indicator(data[5] >> 7),
      section_number(data[6]),
      last_section_number(data[7]),
      dsmcc_data(NULL),
      crc((data[len - 4] << 24) | (data[len - 3] << 16) | (data[len - 2] << 8) | data[len - 1]),
      belong(NULL)
{
    if(table_id != 0x3B && table_id != 0x3C)
    {
        throw DsmccErr();
    }

    dsmcc_data = new uint8_t[len - 12];
    memcpy(dsmcc_data, data + 8, len - 12); 
}

DsmccSection::~DsmccSection()
{
    if(dsmcc_data != NULL)
        delete []dsmcc_data;
}

void DsmccSection::resolved()
{

}

void DsmccSection::setBelong(ESGInfo* bl)
{
    belong = bl;
}

bool DsmccSection::joinTo(SectionFactory* sf)
{
    if(belong != NULL)
    {
        // save data to the according EsgInfo
        belong->saveDsmccData(this);

        if(belong->recvCompleted())
        {
            std::list<ESGInfo*>::iterator eit = sf->esg_stable_list.begin();
            bool find = false;
            for(; eit != sf->esg_stable_list.end(); ++eit)
            {
                if(*belong == *(*eit))
                {
                    find = true;
                    break;
                }
            }

            if(!find)
            {
                sf->esg_list.remove(belong);
                sf->esg_stable_list.push_back(belong);
                belong->resolved();

                ESGInfo* ei = new ESGInfo(belong->service_id);
                ei->pid_list = belong->pid_list;
                sf->esg_list.push_back(ei);
            }
            else
            {
                belong->reset();
            }
        }
    }
    
    return false;
}

ESGInfo::ESGInfo(uint16_t ser_id)
    : service_id(ser_id),
      pid_list(),
      dsi(NULL),
      dii_list(),
      obj_list(),
      check_sum(0)
{

}

ESGInfo::~ESGInfo()
{
    pid_list.clear();

    if(dsi != NULL)
        delete dsi;

    std::set<DII*, cmp_secp<DII>>::iterator dit = dii_list.begin();
    for(; dit != dii_list.end(); ++dit)
    {
        delete (*dit);
    }

    obj_list.clear();
}

void ESGInfo::saveFile(char* save_path, ObjDsmcc* od)
{
    if(od->object_kind[0] == 'f' && od->object_kind[1] == 'i' && od->object_kind[2] == 'l')
    {
        FILE* fp = fopen(save_path, "wb");
        if(fp != NULL)
        {
            fwrite(((ObjFile*)od)->file_content, ((ObjFile*)od)->real_file_length, 1, fp);
            fclose(fp);
        }
    }
    else if((od->object_kind[0] == 'd' && od->object_kind[1] == 'i' && od->object_kind[2] == 'r') ||
            (od->object_kind[0] == 's' && od->object_kind[1] == 'r' && od->object_kind[2] == 'g'))
    {
        mkdir(save_path);
        ObjDsmcc fd;
        std::set<ObjDsmcc*, cmp_secp<ObjDsmcc>>::iterator fit;
        std::list<Binding*>::iterator bit = ((ObjDir*)od)->binding_list.begin();
        for(; bit != ((ObjDir*)od)->binding_list.end(); ++bit)
        {
            char path[256];
            sprintf(path, "%s\\%s", save_path, (*bit)->bname->id_data);
            fd.object_key = (*bit)->object_key;
            if((fit = obj_list.find(&fd)) != obj_list.end())
            {
                if(!(*fit)->saved)
                {
                    saveFile(path, *fit);
                    (*fit)->saved = true;
                }
            }
        }
    }
    else
    {
        return ;
    }
}

void ESGInfo::saveFile(char* save_path)
{
    if(save_path == NULL || strlen(save_path) == 0)
    {
        saveFile(".");
    }
    else
    {
        char path[256];
        sprintf(path, "%s\\%d", save_path, service_id);
        mkdir(path);
        ObjDsmcc od;
        std::set<ObjDsmcc*, cmp_secp<ObjDsmcc>>::iterator fit;
        std::set<ObjDsmcc*, cmp_secp<ObjDsmcc>>::iterator oit = obj_list.begin();
        std::set<ObjDsmcc*, cmp_secp<ObjDsmcc>>::iterator oed = obj_list.end();

        for(; oit != oed; ++oit)
        {
            (*oit)->saved = false;
        }

        oit = obj_list.begin();
        for(; oit != oed; ++oit)
        {
            if(((*oit)->object_kind[0] == 'd' && (*oit)->object_kind[1] == 'i' && (*oit)->object_kind[2] == 'r') ||
               ((*oit)->object_kind[0] == 's' && (*oit)->object_kind[1] == 'r' && (*oit)->object_kind[2] == 'g'))
            {    
                std::list<Binding*>::iterator bit = ((ObjDir*)(*oit))->binding_list.begin();
                for(; bit != ((ObjDir*)(*oit))->binding_list.end(); ++bit)
                {
                    sprintf(path, "%s\\%d\\%s", save_path, service_id, (*bit)->bname->id_data);
                    od.object_key = (*bit)->object_key;
                    if((fit = obj_list.find(&od)) != oed)
                    {
                        if(!(*fit)->saved)
                        {
                            saveFile(path, *fit);
                            (*fit)->saved = true;
                        }
                    }
                }
            }
        }
    }
}

bool ESGInfo::recvCompleted()
{
    if(dsi == NULL)
        return false;

    if(dii_list.size() == 0)
        return false;

    std::set<DII*, cmp_secp<DII>>::iterator dit = dii_list.begin();
    for(; dit != dii_list.end(); ++dit)
    {
        check_sum += (*dit)->check_sum;
        if((*dit)->recv_moudule_number < (*dit)->module_number)
        {
            check_sum = 0;
            return false;
        }
    }

    return true;
}

void ESGInfo::saveDsmccData(DsmccSection* dss)
{
    uint16_t message_id;
    message_id = (dss->dsmcc_data[2] << 8) | dss->dsmcc_data[3];
    if(dss->table_id == 0x3B)
    {
        if(message_id == 0x1006) //dsi
        {
           saveDSIInfo(dss);
        }
        else if(message_id == 0x1002) //dii
        {
            saveDIIInfo(dss);
        }
        else
        {
            std::cout << "unknown message id...\n";
        }
    }
    else if(dss->table_id == 0x3C)
    {
        if(0x1003 != message_id)
        {
            std::cout << "unknown message id...\n";
        }
        else
        {
            saveDDBData(dss);
        }
    }
    else
    {
        std::cout << "unknown table id...\n";
    }
}

void ESGInfo::saveDSIInfo(DsmccSection* dss)
{
    if(dsi == NULL)
    {
        dsi = new DSI(dss->dsmcc_data);
    } 
}


void ESGInfo::saveDIIInfo(DsmccSection* dss)
{
    bool update = false;
    DII* dii = new DII(dss->dsmcc_data);
    std::set<DII*, cmp_secp<DII>>::iterator dit;

    for(dit = dii_list.begin(); dit != dii_list.end(); ++dit)
    {
        if((*dii) == *(*dit))
        {
            delete dii;
            dii = NULL;
            break;
        }
        else
        {
            if(dii->dsmh->transactionId == (*dit)->dsmh->transactionId &&
                dii->check_sum != (*dit)->check_sum)
            {
                update = true;
                break;
            }
        }
    }

    if(update)
    {
        reset();
    }

    if(dii != NULL)
    {
        dii->getDetail();
        dii_list.insert(dii);
    }
}

void ESGInfo::saveDDBData(DsmccSection* dss)
{
    uint8_t adp_len = dss->dsmcc_data[9];
    uint16_t data_len = (dss->dsmcc_data[10] << 8) | dss->dsmcc_data[11];

    uint8_t* pd = dss->dsmcc_data + 12 + adp_len;

    data_len -= adp_len;
    uint16_t md_id = (pd[0] << 8) | pd[1];
    uint16_t blk_num = (pd[4] << 8) | pd[5];

    pd += 6;
    data_len -= 6;

    bool find = false;
    std::set<DII::Module*, cmp_secp<DII::Module>>::iterator fit;
    std::set<DII*, cmp_secp<DII>>::iterator dit = dii_list.begin();
    sh_md.moduleID = md_id;
    for(; dit != dii_list.end(); ++dit)
    {
        if((fit = (*dit)->mod_list.find(&sh_md)) != (*dit)->mod_list.end())
        {
            find = true;
            break;
        }
    }

    if(find)
    {
        if((*fit)->recv_completed)
            return ;
        else
        {
            if((*fit)->block_map[blk_num] == 0xFF)
                return ;
            else
            {
                if((*fit)->block_size * blk_num + data_len > (*fit)->module_size)
                {
                    std::cout << "module size is too big! maybe the dii info has updated...\n";
                    return;
                }
                memcpy((*fit)->module_data + (*fit)->block_size * blk_num, pd, data_len);
                (*fit)->block_map[blk_num] = 0xFF;
                (*fit)->recv_length += data_len;
                if((*fit)->recv_length == (*fit)->module_size)
                {
                    (*fit)->recv_completed = true;
                    (*dit)->recv_moudule_number += 1;
                }
            }
        }
    }
}

bool ESGInfo::operator==(const ESGInfo& ei)
{
    return (service_id == ei.service_id &&
            check_sum == ei.check_sum);
}

void ESGInfo::resolved()
{
    std::set<DII*, cmp_secp<DII>>::iterator dit = dii_list.begin();
    for(; dit != dii_list.end(); ++dit)
    {
        std::set<DII::Module*, cmp_secp<DII::Module>>::iterator mit = (*dit)->mod_list.begin();
        std::set<DII::Module*, cmp_secp<DII::Module>>::iterator med = (*dit)->mod_list.end();
        for(; mit != med; ++mit)
        {
            if((*mit)->position == 0x00)
            {
                uint8_t* ucd = (uint8_t*)malloc((*mit)->raw_module_size * 10);
                uint32_t st = 0;
                std::set<DII::Module*, cmp_secp<DII::Module>>::iterator fit = mit;
                while(fit != med && (*fit)->position != 0xFF)
                {
                    memcpy(ucd + st, (*fit)->module_data, (*fit)->raw_module_size);
                    st += (*fit)->raw_module_size;
                    if((*fit)->position == 0x02)
                        break;
                    sh_md.moduleID = (*fit)->link_module_id;
                    fit = (*dit)->mod_list.find(&sh_md);
                }

                if((*mit)->module_data != NULL)
                {
                    delete (*mit)->module_data;
                    (*mit)->module_data = new uint8_t[st];
                    memcpy((*mit)->module_data, ucd, st);
                    (*mit)->raw_module_size = st;
                }

                free(ucd);           
            }

            (*mit)->resolved();

            std::list<ObjDsmcc*>::iterator oit = (*mit)->obj_list.begin();
            for(; oit != (*mit)->obj_list.end(); ++oit)
            {
                obj_list.insert(*oit);
            }
        }
    }
}

void ESGInfo::reset()
{
    if(dsi != NULL)
    {
        delete dsi;
        dsi = NULL;
    }

    std::set<DII*, cmp_secp<DII>>::iterator dit;
    for(dit = dii_list.begin(); dit != dii_list.end(); ++dit)
    {
        delete (*dit);
    }
    dii_list.clear();
}

