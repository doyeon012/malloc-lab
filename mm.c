/*
 * mm-naive.c - The fastest, least memory-efficient malloc package.
 * 
 * In this naive approach, a block is allocated by simply incrementing
 * the brk pointer.  A block is pure payload. There are no headers or
 * footers.  Blocks are never coalesced or reused. Realloc is
 * implemented directly using mm_malloc and mm_free.
 *
 * NOTE TO STUDENTS: Replace this header comment with your own header
 * comment that gives a high level description of your solution.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>

#include "mm.h"
#include "memlib.h"

#include <stddef.h> // for NULL


/*********************************************************
 * NOTE TO STUDENTS: Before you do anything else, please
 * provide your team information in the following struct.
 ********************************************************/
team_t team = {
    /* Team name */
    "ateam",
    /* First member's full name */
    "Harry Bovik",
    /* First member's email address */
    "bovik@cs.cmu.edu",
    /* Second member's full name (leave blank if none) */
    "",
    /* Second member's email address (leave blank if none) */
    ""
};

/* single word (4) or double word (8) alignment */
#define ALIGNMENT 8

/* rounds up to the nearest multiple of ALIGNMENT */
#define ALIGN(size) (((size) + (ALIGNMENT-1)) & ~0x7)


#define SIZE_T_SIZE (ALIGN(sizeof(size_t)))

/* 기본 상수 및 매크로 */
#define WSIZE 4 
#define DSIZE 8 
#define CHUNKSIZE (1<<12) //4096바이트, 즉4KB를 의미한다. 한 번에 확장할 힙의 크기를 정의한다

#define MAX(x, y) ( (x) > (y) ? (x) : (y) ) 

// 크기와 할당 비트 결합
#define PACK(size,alloc) ( (size)| (alloc) ) //alloc : 가용여부 (ex. 000) / size : block size를 의미. => 합치면 온전한 주소가 나온다.

/* 주어진 포인터 p에서*/
#define GET(p) ( *(unsigned int*)(p) ) // 4바이트 정수 값을 읽는다.(헤더or푸터 블록 크기가 할당 상태 읽을 때 사용)
#define PUT(p, val) ( *(unsigned int*)(p) = (int)(val) ) // 4바이트 정수 값을 저장.(헤더, 푸터 설정시)

/* address p위치로부터 size를 읽고 field를 할당*/
#define GET_SIZE(p) (GET(p) & ~0x7) // 마지막 3비트(할당 상태 포함) 제거(~0x7=마지막 세 비트가 0인 비트 마스크)
#define GET_ALLOC(p) (GET(p) & 0x1) // 할당 상태 나타냄(마지막 비트 0 or 1 - 추출)

/* 주어진 블록 포인터(bp)에서 ~*/
//헤더 and  푸터 주소 계산.
#define HDRP(bp) ( (char*)(bp) - WSIZE) 
#define FTRP(bp) ( (char*)(bp) + GET_SIZE(HDRP(bp) ) - DSIZE ) //다음 블록 bp - DSIZE = 현재 푸터시작

//다음 블록 and 이전 블록 주소 계산
#define NEXT_BLKP(bp) ( (char*)(bp) + GET_SIZE(( (char*)(bp) - WSIZE) )) // GET_SIZE로 현재 블록의 크기 얻은다음 그 만큼 + 하면 다음 블럭 bp로감.
#define PREV_BLKP(bp) ( (char*)(bp) - GET_SIZE(( (char*)(bp) - DSIZE) )) // GET_SIZE로 이전 블록의 크기를 얻은 다음 그 만큼 - 하면 전 블럭 bp로감

static char *heap_listp; //처음에 쓸 큰 가용블록 합을 만들기

// 블록 연결하기
static void *coalesce(void *bp)
{
     size_t prev_alloc = GET_ALLOC(FTRP(PREV_BLKP(bp))); // 그 전 블록으로 가서 그 블록의 가용여부 확인
     size_t next_alloc = GET_ALLOC(HDRP(NEXT_BLKP(bp))); // 그 뒷 블록의 가용 여부 확인
     size_t size = GET_SIZE(HDRP(bp)); 

     //case 1 - 이전 and 다음 모두(할당), 현재 블록의 상태는 할당에서 가용으로 변경
     if ( prev_alloc && next_alloc) 
     {
        return bp; // 이미 free에서 가용이 되었으니 여기선 따로 free 할 필요 없다.
     }

     // case 2 - 이전(할당), 다음(가용). 다음 블록으로 합치기o
     else if (prev_alloc && !next_alloc)
     {
        size = size + GET_SIZE(HDRP(NEXT_BLKP(bp))); // 다음 블록의 헤더를 보고 그 블록의 크기만큼 지금 블록의 사이즈에 추가한다.
        PUT(HDRP(bp), PACK(size, 0)); // 헤더 업데이트(더 큰 크기로 PUT)
        PUT(FTRP(bp), PACK(size, 0)); // 푸터 업데이트
     }
     
     //case 3 - 이전(가용), 다음(할당). 이전 블록과 합치기o
     else if (!prev_alloc && next_alloc)
     {
        size = size + GET_SIZE(HDRP(PREV_BLKP(bp)));
        PUT(FTRP(bp), PACK(size, 0)); // 푸터에 먼저 조정하려는 크기로 상태 변경
        PUT(HDRP(PREV_BLKP(bp) ), PACK(size, 0)); // 현재 헤더에서 그 앞블록의 헤더 위치로 이동한 다음에, 조정한 size를 넣는다. 
        bp = PREV_BLKP(bp); // bp는 그 전블록으로 전블록과 현재가 합쳐졌기 때문에
     }

     // case 4 이전 and 다음 모두(가용). 이전, 다음, 블록 모두 합치기o
     else 
     {
        size = size + GET_SIZE(HDRP(PREV_BLKP(bp))) + GET_SIZE(FTRP(NEXT_BLKP(bp))); // 이전 블록 헤더, 다음 블록 푸터 까지로 사이즈 늘리기
        PUT(HDRP(PREV_BLKP(bp)), PACK(size, 0)); // 헤더부터 앞으로 가서 사이즈 넣음.
        PUT(FTRP(NEXT_BLKP(bp)), PACK(size, 0)); // 푸터를 뒤로 가서 사이즈 넣는다.
        bp = PREV_BLKP(bp); // bp는 그 전블록으로 전블록과 현재 후블록이 합쳐졌기 때문에
     }

     return bp;
}


// 힙을 주어진 워드 수만큼 확장
static void *extend_heap(size_t words)
{
    char *bp;
    size_t size; 
    // 홀수인 경우 짝수로 조정(alignment유지)
    size = (words % 2) ? (words + 1) * WSIZE : words * WSIZE; // 8의 배수 맞추기 위함

    //홀수가 나오면 사이즈를 한번 더 재정의. 힙에서 늘릴 사이즈를 재정의.
    if ((long)(bp = mem_sbrk(size)) == -1)
    {
        return NULL;
    } // 사이즈 늘릴 때마다 old brk는 과거의 mem_brk 위치로 감.

    // 새 프리 블록의 헤더와 푸터 초기화
    PUT(HDRP(bp), PACK(size, 0)); // regular block의 총합의 첫번째 부분. 현재 bp 위치의 한 칸 앞에 헤더를 생성. 
    PUT(FTRP(bp), PACK(size, 0)); // regular block 총합의 마지막 부분.
    
    //에필로그 헤더 업데이트.
    PUT(HDRP(NEXT_BLKP(bp)), PACK(0, 1));
    
    // 이전 블록이 프리 상태인 경우 병합
    return coalesce(bp);

}

// malloc 패키지 초기화 
int mm_init(void)
{   
    //초기 빈 힙 생성(묵시적 가용 리스트)
    if ((heap_listp = mem_sbrk(4*WSIZE)) == (void*)-1)
    {
        return -1; // 힙 공간 확장 실패 시 -1 반환
    }

    PUT(heap_listp, 0); //패딩 생성
    PUT(heap_listp + (1*WSIZE), PACK(DSIZE, 1)); // 프롤로그 헤더 생성.
    PUT(heap_listp + (2*WSIZE), PACK(DSIZE, 1)); // 프롤로그 푸터 생성.
    PUT(heap_listp + (3*WSIZE), PACK(0, 1)); // 에필로그 헤더 초기 설정
    heap_listp = heap_listp + (2*WSIZE); //프롤로그 블록 다음으로 이동

    // 초기 힙 확장
    if (extend_heap(CHUNKSIZE/WSIZE) == NULL)
        return -1;

    return 0; // 성공적 초기화
}

// // first-fit
// static void *find_fit(size_t asize)
// {
//     void *bp;

//     //힙의 시작부터 탐색을 시작하여 에필로그 헤더가 나올 때까지 반복
//     for (bp=heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) 
//     {

//         // 현재 블록이 할당(x) && (asize를 수용할 수 있는 크기인지 확인)
//         if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) 
//         {
//             return bp; 
//         }
//     }
//     return NULL; 
// }

static char *heap_listp = NULL;  // 전역으로 힙의 시작 주소를 관리
static char *last_bp = NULL;     // 마지막으로 반환하거나 탐색한 블록의 위치

//next-fit
static void *find_fit(size_t asize)
{
    char *bp;

    // last_bp가 설정되지 않았다면, 힙의 시작에서부터 탐색을 시작
    if (last_bp == NULL) {
        last_bp = heap_listp;
    }

    // 첫 번째 탐색: last_bp에서 시작하여 힙의 끝까지
    for (bp = last_bp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            last_bp = bp;  // 탐색 종료 위치 갱신
            return bp;
        }
    }

    // 끝까지 찾지 못했다면, 힙의 시작부터 last_bp 직전까지 다시 탐색
    for (bp = heap_listp; bp < last_bp; bp = NEXT_BLKP(bp)) {
        if (!GET_ALLOC(HDRP(bp)) && (asize <= GET_SIZE(HDRP(bp)))) {
            last_bp = bp;  // 탐색 종료 위치 갱신
            return bp;
        }
    }

    // 적합한 블록을 찾지 못했으면 NULL 반환
    return NULL;
}

// static char *heap_listp = NULL;  // 전역으로 힙의 시작 주소를 관리

// // 힙에서 asize 크기의 블록을 찾는 best-fit 방식의 함수
// static void *find_fit(size_t asize)
// {
//     char *bp;
//     char *best_fit = NULL;
//     size_t smallest_diff = (size_t)-1; // 최소 차이를 저장, 초기값은 최대 크기

//     for (bp = heap_listp; GET_SIZE(HDRP(bp)) > 0; bp = NEXT_BLKP(bp)) {
//         size_t csize = GET_SIZE(HDRP(bp));
//         if (!GET_ALLOC(HDRP(bp)) && (asize <= csize)) {
//             size_t diff = csize - asize;
//             if (diff < smallest_diff) {
//                 smallest_diff = diff;
//                 best_fit = bp;
//                 if (diff == 0) break; // 완벽하게 맞는 블록을 찾은 경우 더 이상 탐색하지 않음
//             }
//         }
//     }

//     return best_fit; // 가장 적합한 블록의 주소를 반환, 적합한 블록을 찾지 못했다면 NULL 반환
// }


// 블록 할당. 필요시 남은 부분 다시 가용 블록
static void place(void *bp, size_t asize)
{
    //현재 메모리 블록의 총 크기를 헤더에서 읽어온다.
    size_t csize = GET_SIZE(HDRP(bp));
    
    // 요청된 크기 asize와 현재 블록 크기 csize를 비교하여 충분한 공간이 남는지 확인한다.
    if ( (csize-asize) >= (2*DSIZE)) // 남는 공간이 (헤더+푸터)+최소한의 데이터
    {   
        // 충분한 공간이 남을 경우 > 현재 블록을 asize 크기의 할당된 블록으로 분할
        PUT(HDRP(bp), PACK(asize, 1)); // 새 할당된 블록의 헤더 설정 : 할당(1)
        PUT(FTRP(bp), PACK(asize, 1)); // 새 할당된 블록의 푸터 설정 : 

        // 분할 후 남은 부분을 새로운 가용 블록으로 설정
        bp = NEXT_BLKP(bp); // 분할된 첫 번째 블록 뒤의 새로운 가용 블록 포인터로 이동
        PUT(HDRP(bp), PACK(csize-asize, 0)); // 남은 블록의 헤더 : 가용(0)
        PUT(FTRP(bp), PACK(csize-asize, 0)); // 남은 블록의 푸터 : 
    }

    // 충분한 공간이 남지 않을 경우 > 전체 블록을 asize 크기의 할당된 블록으로 사용
    else
    {
        PUT(HDRP(bp), PACK(csize, 1)); // 전체 블록의 헤더 설정 : 전체 크기와 할당 표시(1)
        PUT(FTRP(bp), PACK(csize, 1)); // 전체 블록의 푸터 설정 : 위와 동일한 값
    }
}

// 가용 리스트에 블록 할당
void *mm_malloc(size_t size)
{
    size_t asize; // 블록의 실제 요청 크기
    size_t extendsize; // 적합한 블록을 찾지 못했을 경우, 힙을 확장하기 위해 요청할 크기
    char *bp;

    if (size == 0) return NULL; 

    // 요청 사이즈가 8보다 작을때
    if (size <= DSIZE)
    {
        asize = 2*DSIZE; // 최소 블록 크기 설정(헤더 + 푸터 = 8)
    }

    else
    {
        //메모리 블록은 오버헤드(헤더와 푸터)와 정렬 요구 사항 충족.
        asize = DSIZE * ( (size + (DSIZE) + (DSIZE - 1)) / DSIZE );
    }

    // 가용 블록 탐색
    if ((bp = find_fit(asize)) != NULL)
    {
        place(bp, asize); //적합한 블록을 찾으면 place 함수 호출하여 해당 메모리 할당, 블록의 시작 주소 반환
        return bp;
    }

    //적합한 가용 블록 찾지 못했다.
    extendsize = MAX(asize, CHUNKSIZE); // 힙 확장 시 충분한 공간 확보를 위함.

    if( (bp = extend_heap(extendsize / WSIZE)) == NULL )
    {
        // CHUNKSIZE는 블록을 늘리는 양이고, MAX_ADDR은 힙의 최대 크기라서 두개는 다르다. 인자로 들어가는 건 단위 블록 수
        return NULL;
    }

    //확장된 힙에서 메모리 할당
    place(bp, asize);
    return bp;
}

// 블록을 가용 상태로 변경. 
void mm_free(void *bp)
{
    size_t size = GET_SIZE(HDRP(bp));
    PUT(HDRP(bp), PACK(size, 0)); 
    PUT(FTRP(bp), PACK(size, 0));

    last_bp = coalesce(bp);
}

// 메모리 재할당(새로운 크기의 메모리가 필요시 새로 할당.
// 기존 데이터 새 위치로 복사한 뒤 기존 블록 해제)
void *mm_realloc(void *bp, size_t size){
    
    if(size <= 0){ 
        mm_free(bp);
        return 0;
    }

    // 입력 포인터 검증(굳이?)
    if(bp == NULL){ // 새로운 메모리 할당을 요구하는 것
        return mm_malloc(size); 
    }

    //새 메모리 할당
    void *newp = mm_malloc(size); 
    if(newp == NULL){
        return 0;
    }

    // 기존 데이터 복사
    size_t oldsize = GET_SIZE(HDRP(bp));
    if(size < oldsize){
    	oldsize = size; 
	}
    memcpy(newp, bp, oldsize); // oldsize 만큼의 데이터를 새 메모리 위치 newp로 복사
    
    // 기존 블록 해제
    mm_free(bp);

    return newp;
}














