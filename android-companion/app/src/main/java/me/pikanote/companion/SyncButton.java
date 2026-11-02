package me.pikanote.companion;

import android.animation.ValueAnimator;
import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.graphics.RectF;
import android.view.View;
import android.view.animation.LinearInterpolator;

/** The touch target and background stay fixed; only the arrow artwork rotates. */
final class SyncButton extends View {
 private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
 private final Path arrow=new Path();
 private ValueAnimator animator;
 private float angle;
 SyncButton(Context context){super(context);setClickable(true);setFocusable(true);setContentDescription("Reconnect and synchronize");}
 void setBusy(boolean busy){
  if(busy&&animator==null){animator=ValueAnimator.ofFloat(0,360);animator.setDuration(1000);animator.setRepeatCount(ValueAnimator.INFINITE);animator.setInterpolator(new LinearInterpolator());animator.addUpdateListener(a->{angle=(float)a.getAnimatedValue();invalidate();});animator.start();}
  else if(!busy&&animator!=null){animator.cancel();animator=null;angle=0;invalidate();}
 }
 @Override protected void onDraw(Canvas canvas){
  super.onDraw(canvas);float cx=getWidth()/2f,cy=getHeight()/2f,r=Math.min(cx,cy);
  paint.setStyle(Paint.Style.FILL);paint.setColor(isPressed()?0xffd7422b:0xfffa5438);canvas.drawCircle(cx,cy,r,paint);
  canvas.save();canvas.rotate(angle,cx,cy);canvas.translate(cx,cy);float unit=r/24;
  paint.setColor(0xffffffff);paint.setStyle(Paint.Style.STROKE);paint.setStrokeWidth(2.2f*unit);paint.setStrokeCap(Paint.Cap.ROUND);
  RectF arc=new RectF(-8*unit,-8*unit,8*unit,8*unit);canvas.drawArc(arc,35,140,false,paint);canvas.drawArc(arc,215,140,false,paint);
  arrow.reset();arrow.moveTo(-11*unit,-1*unit);arrow.lineTo(-8*unit,2*unit);arrow.lineTo(-5*unit,-1*unit);canvas.drawPath(arrow,paint);
  arrow.reset();arrow.moveTo(5*unit,1*unit);arrow.lineTo(8*unit,-2*unit);arrow.lineTo(11*unit,1*unit);canvas.drawPath(arrow,paint);canvas.restore();
 }
 @Override protected void drawableStateChanged(){super.drawableStateChanged();invalidate();}
 @Override protected void onDetachedFromWindow(){setBusy(false);super.onDetachedFromWindow();}
}
