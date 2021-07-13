#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cassert>
#include <functional>


using namespace std;

#define FILENAME_LEN_MAX 28
#define DIR_ENTRIES_MAX 128
#define OPEN_FILES_MAX 8
#define SECTOR_SIZE 512
#define DEVICE_SIZE_MAX (1024 * 1024 * 1024)
#define DEVICE_SIZE_MIN (8 * 1024 * 1024)

struct TFile
{
    char m_FileName[FILENAME_LEN_MAX + 1];
    size_t m_FileSize;
};

struct TBlkDev
{
    size_t m_Sectors;
    function<size_t(size_t, void *, size_t)> m_Read;
    function<size_t(size_t, const void *, size_t)> m_Write;
};

struct File
{
    char m_name [FILENAME_LEN_MAX];
    int m_size = 0;
    int m_data = -1;
};


class CFileSystem
{
public:
    File m_files [DIR_ENTRIES_MAX];
    int * m_linkedList;
    TBlkDev m_dau;
    int32_t m_firstFreeSector;
    int32_t m_currentFileIndex;
    int32_t m_crp [128] = {};
    int32_t LL_PADDING;


    CFileSystem (const TBlkDev &blockAccessUnit)
    {
        m_dau = blockAccessUnit;
    }

    void define (const size_t size)
    {
        m_linkedList = new int [size];
    }

    ~CFileSystem ( void )
    {
    }


    static bool CreateFs(const TBlkDev &dev)
    {
        char data [SECTOR_SIZE] = {};
        int t = -2;

        for (int i = 0; i < 10; i++) {
            for (int j = 0; j < 14; j++) {
                memcpy (data + 32 + (j * 36), &t, 4);
            }
            dev . m_Write (i, data, 1);
        }

        t = 0;
        memset (data, '\0', SECTOR_SIZE);
        memcpy (data, &t, 4);

        dev . m_Write (10, data, 1);

        int amountOfSectorsForLL = (dev . m_Sectors - 11) / 128;
        if ((dev . m_Sectors - 11) % 128 != 0)
            amountOfSectorsForLL ++;


        size_t cnt = 1;
        for (int i = 0; i < amountOfSectorsForLL; i++)
        {
            for (int j = 0; j < 512; j += 4)
            {
                t = 1 + j / 4 + (128 * i);
                if (cnt >= dev . m_Sectors - 11 - amountOfSectorsForLL)
                    t = -1;

                memcpy (data + j, &t, 4);

                cnt ++;
            }
            dev . m_Write (11 + i, data, 1);
        }

        return true;
    }

    /** Create structure m_files from dev */
    static CFileSystem * Mount(const TBlkDev &dev)
    {
        CFileSystem * fs = new CFileSystem (dev);

        char data [SECTOR_SIZE] = {};

        for (int i = 0; i < 9; i++)
        {
            dev . m_Read (i, data, 1);
            for (int j = 0; j < 14; j++)
            {
                memcpy (&(fs -> m_files[j + (14 * i)]), data + (36 * j), 36);
            }
        }
        dev . m_Read (9, data, 1);
        memcpy (&(fs -> m_files[126]), data, 36);
        memcpy (&(fs -> m_files[127]), data + 36, 36);

        dev . m_Read (10, data, 1);
        memcpy (&(fs -> m_firstFreeSector), data, 4);


        int amountOfSectorsForLL = (dev . m_Sectors - 11) / 128;
        if ((dev . m_Sectors - 11) % 128 != 0)
            amountOfSectorsForLL ++;

        fs -> LL_PADDING = amountOfSectorsForLL + 11;

        

        fs -> define (amountOfSectorsForLL * 128);

        memset (data, '\0', 512);
        for (int i = 0; i < amountOfSectorsForLL; i++)
        {
            dev . m_Read (11 + i, data, 1);
            for (int j = 0; j < 512; j += 4)
            {
                memcpy (&(fs -> m_linkedList [(i * 128) + j / 4]), data + j, 4);
            }
        }


        return fs;
    }

    /** Saves structure to disk */
    bool Umount ( void )
    {
        char data[SECTOR_SIZE] = {};
        for (int i = 0; i < 9; i++)
        {
            for (int j = 0; j < 14; j++)
            {
                memcpy (data + (36 * j), &(m_files[j + (14 * i)]), 36);
            }
            m_dau . m_Write (i, data, 1);
        }

        memset (data, '\0', SECTOR_SIZE);
        memcpy (data, &(m_files[126]), 36);
        memcpy (data + 36, &(m_files[127]), 36);
        
        m_dau . m_Write (9, data, 1);

        memset (data, '\0', SECTOR_SIZE);
        memcpy (data, &m_firstFreeSector, 4);
        m_dau . m_Write (10, data, 1);


        int amountOfSectorsForLL = (m_dau . m_Sectors - 11) / 128;
        memset (data, '\0', SECTOR_SIZE);
        int cnt = 0;
        for (int i = 0; i < amountOfSectorsForLL; i++)
        {
            for (int j = 0; j < 512; j += 4)
            {
                memcpy (data + j, &(m_linkedList[j / 4 + i * 128]), 4);
                cnt ++;
            }
            m_dau . m_Write (11 + i, data, 1);
        }


        delete[] m_linkedList;
        return true;
    }

    size_t FileSize(const char *fileName)
    {
        int fd = findFile (fileName);
        if (fd == -1)
            return SIZE_MAX;
        return m_files[fd] . m_size;
    }

    /** Opens or creates file
     * @returns file descriptor for existing file, -1 for writeMode==false and file_doesn't exist 
     */
    int OpenFile(const char *fileName, bool writeMode)
    {
        int fd = findFile (fileName);

        if (writeMode)
        {
            if (fd == -1)
            {
                return addFile (fileName);
            } else {
                DeleteFile (fileName);
                return addFile (fileName);
            }
        }
        if (fd == -1)
            return -1;
        
        return fd;
    }

    bool CloseFile(int fd)
    {
        if (fd < 0 || fd > 127)
            return false;
        if (m_files[fd] . m_data == -2)
            return false;
        m_crp [fd] = 0;
        return true;
    }



    size_t ReadFile(int fd, void *data, size_t len)
    {
        if (m_files [fd] . m_data == -2 || fd >= 128)
            return 0;
        
        char sector [SECTOR_SIZE] = {};
        int * offset = &(m_crp [fd]);

        if (len > (unsigned)(m_files[fd] . m_size - *offset))
        {
            if (m_files[fd] . m_size - *offset == 0)
                return 0;
            
            int a = *offset / SECTOR_SIZE; // 0

            int current_unit = m_files[fd] . m_data;

            for (int i = 0; i < a; i++)
                current_unit = m_linkedList[current_unit];


            m_dau . m_Read (current_unit + LL_PADDING, sector, 1);

            if (m_files[fd] . m_size / SECTOR_SIZE == *offset / SECTOR_SIZE) {
                memcpy (data, sector + (*offset % SECTOR_SIZE), m_files[fd] . m_size % SECTOR_SIZE - *offset % SECTOR_SIZE);

                int ret = m_files[fd] . m_size % SECTOR_SIZE - *offset % SECTOR_SIZE;
                *offset += m_files[fd] . m_size % SECTOR_SIZE - *offset % SECTOR_SIZE;
                return ret;
            }
            else
                memcpy (data, sector + (*offset % SECTOR_SIZE), SECTOR_SIZE - (*offset % SECTOR_SIZE));


            int cnt = 0;
            while (m_linkedList[current_unit] != -1)
            {
                current_unit = m_linkedList[current_unit];
                if (m_linkedList[current_unit] == -1) {
                    int t = m_files[fd] . m_size % SECTOR_SIZE;
                    if (t == 0)
                        t = SECTOR_SIZE;
                    memset (sector, '\0', SECTOR_SIZE);
                    m_dau . m_Read (current_unit + LL_PADDING, sector, 1);
                    memcpy ((char*)data + (SECTOR_SIZE * cnt) + (SECTOR_SIZE - (*offset % SECTOR_SIZE)), sector, t);
                    break;
                }
                m_dau . m_Read (current_unit + LL_PADDING, sector, 1);
                memcpy ((char*)data + (SECTOR_SIZE * cnt) + (SECTOR_SIZE - (*offset % SECTOR_SIZE)), sector, SECTOR_SIZE);

                cnt ++;
            }

            int ret = (unsigned)(m_files[fd] . m_size - *offset);
            *offset = m_files[fd] . m_size;
            return ret;
        }

        if (*offset == 0)
        {
            int a = len / SECTOR_SIZE;
            int b = len % SECTOR_SIZE;

            int next_unit = m_files[fd] . m_data;
            
            for (int i = 0; i < a; i++)
            {
                m_dau . m_Read (next_unit + LL_PADDING, sector, 1);
                memcpy ((char*)data + (i * SECTOR_SIZE), sector, SECTOR_SIZE);
                next_unit = m_linkedList[next_unit];
            }

            if (b != 0)
            {
                memset (sector, '\0', sizeof(sector));
                m_dau . m_Read (next_unit + LL_PADDING, sector, 1);
                memcpy ((char*)data + (SECTOR_SIZE * a), sector, b);
            }

            *offset += len;
            return len;
        }
        else
        {
            int a = *offset / SECTOR_SIZE;
            int current_unit = m_files[fd] . m_data;

            for (int i = 0; i < a; i++)
                current_unit = m_linkedList[current_unit];

            m_dau . m_Read (current_unit + LL_PADDING, sector, 1);

            if (len < (unsigned)SECTOR_SIZE - *offset % SECTOR_SIZE)
            {
                memcpy (data, sector + (*offset % SECTOR_SIZE), len);
                *offset += len;
                return len;
            }
            memcpy (data, sector + (*offset % SECTOR_SIZE), SECTOR_SIZE - *offset % SECTOR_SIZE);


            size_t tmp_len = len - (SECTOR_SIZE - *offset % SECTOR_SIZE);
            a = tmp_len / SECTOR_SIZE; // 0
            int b = tmp_len % SECTOR_SIZE; // 48


            current_unit = m_linkedList[current_unit];

            for (int i = 0; i < a; i++)
            {
                m_dau . m_Read (current_unit + LL_PADDING, sector, 1);
                memcpy ((char*)data + (SECTOR_SIZE - *offset % SECTOR_SIZE) + (SECTOR_SIZE * i), sector, SECTOR_SIZE);

                current_unit = m_linkedList[current_unit];
            }
            if (b != 0)
            {
                memset (sector, '\0', SECTOR_SIZE);
                m_dau . m_Read (current_unit + LL_PADDING, sector, 1);



                memcpy ((char*)data + (SECTOR_SIZE - *offset % SECTOR_SIZE) + (SECTOR_SIZE * a), sector, b);
            }


            *offset += len;
            return len;
        }
    }


    size_t WriteFile(int fd, const void *data, size_t len)
    {
        if (m_files[fd] . m_data == -2 || fd >= 128 || fd < 0)
            return 0;
        
        char buff [SECTOR_SIZE] = {};
        memset (buff, '\0', SECTOR_SIZE);


        int last_unit = m_files[fd] . m_data;
        if (m_files[fd] . m_size != 0)
        {
            while (true)
            {
                if (m_linkedList[last_unit] != -1)
                    last_unit = m_linkedList[last_unit];
                else
                    break;
            }


            if (m_files[fd] . m_size % SECTOR_SIZE == 0)
            {
                if (m_firstFreeSector == -1)
                {
                    return 0;
                }
                m_linkedList[last_unit] = m_firstFreeSector;
                last_unit = m_firstFreeSector;
                m_firstFreeSector = m_linkedList[m_firstFreeSector];
                m_linkedList[last_unit] = -1;
            }

            
            if (len < (unsigned)(SECTOR_SIZE - m_files[fd] . m_size % SECTOR_SIZE))
            {
                // We can put whole new data to the end of block and it'll fit in there
                m_dau . m_Read (last_unit + LL_PADDING, buff, 1);


                memcpy (buff + m_files[fd] . m_size % SECTOR_SIZE, data, len);
                m_dau . m_Write (last_unit + LL_PADDING, buff, 1);

                m_files[fd] . m_size += len;

                return len;
            }

            m_dau . m_Read (last_unit + LL_PADDING, buff, 1);
            memcpy (buff + m_files[fd] . m_size % SECTOR_SIZE, data, SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE));


            m_dau . m_Write (last_unit + LL_PADDING, buff, 1);

            if (m_firstFreeSector == -1)
            {
                m_files[fd] . m_size += SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE);
                return SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE);
            }


            int a = (len - (SECTOR_SIZE - m_files[fd] . m_size % SECTOR_SIZE)) / SECTOR_SIZE;
            int b = (len - (SECTOR_SIZE - m_files[fd] . m_size % SECTOR_SIZE)) % SECTOR_SIZE;


            if (a == 0)
            {
                if (b == 0) {
                    m_files[fd] . m_size += len;
                    return len;
                }

                memset (buff, '\0', sizeof(buff));

                memcpy (buff, (char*)data + (SECTOR_SIZE - m_files[fd] . m_size % SECTOR_SIZE), b);
                m_dau . m_Write (m_firstFreeSector + LL_PADDING, buff, 1);

                m_linkedList[last_unit] = m_firstFreeSector;
                last_unit = m_firstFreeSector;
                m_firstFreeSector = m_linkedList[m_firstFreeSector];
                m_linkedList[last_unit] = -1;

                m_files[fd] . m_size += len;
                return len;
            }


            for (int i = 0; i < a; i++)
            {
                memcpy (buff, (char*)data + (SECTOR_SIZE - m_files[fd] . m_size % SECTOR_SIZE) + (SECTOR_SIZE * i), SECTOR_SIZE);
                m_dau . m_Write (m_firstFreeSector + LL_PADDING, buff, 1);


                m_linkedList[last_unit] = m_firstFreeSector;
                last_unit = m_firstFreeSector;
                m_firstFreeSector = m_linkedList[m_firstFreeSector];
                m_linkedList[last_unit] = -1;

                if (m_firstFreeSector == -1)
                {
                    m_files[fd] . m_size += (SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE)) + SECTOR_SIZE*i;
                    return (SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE)) + SECTOR_SIZE*i;
                }
            }


            if (b != 0)
            {
                memset (buff, '\0', sizeof(buff));

                memcpy (buff, (char*)data + (SECTOR_SIZE - m_files[fd] . m_size % SECTOR_SIZE) + (SECTOR_SIZE * a), b);
                m_dau . m_Write (m_firstFreeSector + LL_PADDING, buff, 1);


                m_linkedList[last_unit] = m_firstFreeSector;
                last_unit = m_firstFreeSector;
                m_firstFreeSector = m_linkedList[m_firstFreeSector];
                m_linkedList[last_unit] = -1;

                if (m_firstFreeSector == -1)
                {
                    m_files[fd] . m_size += (SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE)) + SECTOR_SIZE*a + b;
                    return (SECTOR_SIZE - (m_files[fd] . m_size % SECTOR_SIZE)) + SECTOR_SIZE*a + b;
                }
            }

            m_files[fd] . m_size += len;
            return len;
        }

        if (m_firstFreeSector == -1)
            return 0;
        else
        {
            m_files[fd] . m_data = m_firstFreeSector;
            m_firstFreeSector = m_linkedList[m_firstFreeSector];
            m_linkedList[m_files[fd] . m_data] = -1;
        }


        int a = len / SECTOR_SIZE;
        int b = len % SECTOR_SIZE;  

        int next_unit = m_files[fd] . m_data;
        if (a == 0)
        {
            memcpy (buff, (char*)data, b);
            m_dau . m_Write (next_unit + LL_PADDING, buff, 1);
            m_files [fd] . m_size += len;
            return len;
        }


        memcpy (buff, (char*)data, SECTOR_SIZE);
        m_dau . m_Write (next_unit + LL_PADDING, buff, 1);
        m_linkedList[next_unit] = -1;


        if (m_firstFreeSector == -1)
        {
            m_files[fd] . m_size += SECTOR_SIZE;
            return SECTOR_SIZE;
        }


        int prev_unit = next_unit;

        for (int i = 1; i < a; i ++)
        {
            memcpy (buff, (char*)data + (i * SECTOR_SIZE), SECTOR_SIZE);
            m_dau . m_Write (m_firstFreeSector + LL_PADDING, buff, 1);
            m_linkedList[prev_unit] = m_firstFreeSector;
            m_firstFreeSector = m_linkedList[m_firstFreeSector];
            prev_unit = m_linkedList[prev_unit];
            m_linkedList[prev_unit] = -1;

            if (m_firstFreeSector == -1)
            {
                m_files[fd] . m_size += SECTOR_SIZE + SECTOR_SIZE*i;
                return SECTOR_SIZE + SECTOR_SIZE*i;
            }
        }

        if (b != 0)
        {
            if (a == 0) {
                memcpy (buff, (char*)data, b);
                m_dau . m_Write (m_files[fd] . m_data + LL_PADDING, buff, 1);
            } else {
                memcpy (buff, (char*)data + (SECTOR_SIZE * a), b);
                m_dau . m_Write (m_firstFreeSector + LL_PADDING, buff, 1);
                m_linkedList[prev_unit] = m_firstFreeSector;
                m_firstFreeSector = m_linkedList[m_firstFreeSector];
                prev_unit = m_linkedList[prev_unit];
                m_linkedList[prev_unit] = -1;
                
                if (m_firstFreeSector == -1)
                {
                    m_files[fd] . m_size += SECTOR_SIZE*a + b;
                    return SECTOR_SIZE*a + b;
                }
            }
        }

        m_files[fd] . m_size += len;
        return len;
    }

    void deleteFileHelper (const size_t fd)
    {
        char buff [SECTOR_SIZE] = {};
        int tmp = m_files[fd] . m_data;


        if (tmp == -1)
        {
            m_files[fd] . m_data = -2;
            char empty_name [FILENAME_LEN_MAX] = {};

            memcpy (m_files[fd] . m_name, empty_name, FILENAME_LEN_MAX);
            m_files[fd] . m_size = 0;
            return;
        }



        int next = m_linkedList[tmp];

            while (tmp != -1)
            {
                m_dau . m_Write (tmp + LL_PADDING, buff, 1);
                m_linkedList[tmp] = m_firstFreeSector;
                m_firstFreeSector = tmp;
                tmp = next;
                if (next != -1)
                    next = m_linkedList[next];
                else {
                    break;
                }
            }

        m_files[fd] . m_data = -2;
        char empty_name [FILENAME_LEN_MAX] = {};

        memcpy (m_files[fd] . m_name, empty_name, FILENAME_LEN_MAX);
        m_files[fd] . m_size = 0;
    }

    bool DeleteFile(const char *fileName)
    {
        int fd = -1;
        for (int i = 0; i < 128; i++) {
            if (strcmp (m_files[i] . m_name, fileName) == 0 && m_files[i] . m_data != -2) {
                fd = i;
                break;
            }
        }

        if (fd == -1)
            return false;

        deleteFileHelper (fd);

        return true;
    }


    bool FindFirst(TFile &file)
    {
        m_currentFileIndex = -1;
        for (int i = 0; i < 128; i++)
        {
            if (m_files[i] . m_data != -2)
            {
                m_currentFileIndex = i;
                break;
            }
        }
        if (m_currentFileIndex == -1)
            return false;

        memcpy (file . m_FileName, m_files[m_currentFileIndex].m_name, 28);
        file . m_FileSize = m_files[m_currentFileIndex] . m_size;

        m_currentFileIndex ++;

        return true;
    }

    bool FindNext(TFile &file)
    {
        bool found = false;
        for (int i = m_currentFileIndex; i < 128; i++)
        {
            if (m_files[i] . m_data != -2)
            {
                found = true;
                m_currentFileIndex = i;
                break;
            }
        }

        if (! found )
            return false;

        memcpy (file . m_FileName, m_files[m_currentFileIndex].m_name, 28);
        file . m_FileSize = m_files[m_currentFileIndex] . m_size;

        m_currentFileIndex ++;

        return true;
    }

    size_t get_free_sectors ( void )
    {
        int ff = m_firstFreeSector;
        size_t cnt = 0;

        if (ff == -1)
            return cnt;
        
        while (m_linkedList[ff] != -1) {
            cnt ++;
            ff = m_linkedList[ff];
        }
        return cnt;
    }

    void print_info (int fd)
    {
        for (int i = 0; i < 28; i++)
            printf ("%c", m_files[fd] . m_name[i]);
        printf ("\n%d\n", m_files[fd] . m_size);
        printf ("%d\n", m_files[fd] . m_data);
    }

private:

    int findFile (const char *fileName)
    {
        for (int i = 0; i < 128; i++)
            if (strcmp (m_files[i] . m_name, fileName) == 0 && m_files[i] . m_data != -2)
                return i;
        return -1;
    }
    
    int addFile (const char *fileName)
    {
        int i = 0;
        while (m_files[i] . m_data != -2) {
            if (i == 127)
                return -1;
            i ++;
        }   

        strcpy (m_files[i] . m_name, fileName);
        m_files[i] . m_size = 0;
        m_files[i] . m_data = -1;

        return i;
    }
};
