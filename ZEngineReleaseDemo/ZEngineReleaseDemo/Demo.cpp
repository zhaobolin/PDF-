#include <stdio.h>
#include "Extract.h"
#include "Ipe_Lines.h"
#include "MuInclude.h"
#include "Ipe_PdfPath.h"
#include "Ipe_PdfPage.h"
#include "Ipe_PdfDocument.h"

using namespace std;
void main ()
{
	char* input="C:\\Users\\�Բ���\\Desktop\\TestPDF\\��ɫ��ü���ˮƽ.pdf";//����Ҫ������PDF�ļ�·��
	cliprect rect;
	rect.x0=100;
	rect.y0=300;
	rect.x1=500;
	rect.y1=200;
	Ipe_PdfDocument * document=new Ipe_PdfDocument(input);//��ָ����PDF�ļ�������,���ڴ��в���һ��Document
	Ipe_node<Ipe_PdfPage>* page=document->getlist()->headler;//һ��Document��Ӧһ��PDF�ļ�,��ʱʹ��һ��page�ṹ��ȡDocument��ҳ������ͷ
	page=page->next;//��ʱָ��ָ�����PDF�ļ��ĵ�һҳ

	page->t->clipwithrect(&rect);//�ü�����
	page->t->pagetoSVG("C:\\Users\\�Բ���\\Desktop\\����1.svg");//�˺�����PDF��ָ��ҳ����תΪSVG�洢,·���Լ��趨
	//page->t->printpage();

	system("pause");
}